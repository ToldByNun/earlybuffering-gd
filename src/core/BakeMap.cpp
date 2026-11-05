#include "BakeMap.hpp"
#include "AssistClock.hpp"
#include "AssistContracts140.hpp"
#include "AssistController.hpp"
#include "BakeSpawnProbe.hpp"
#include "ClickBakeStore.hpp"
#include "LevelFingerprint.hpp"
#include "LevelCacheIndex.hpp"
#include "ShadowSimulator.hpp"
#include "Tg2BakeFixture.hpp"
#include "TriggerWorldBaker.hpp"
#include "TriggerWorldStore.hpp"
#include "LevelProfile.hpp"
#include "RoutePathBuilder.hpp"
#include "RoutePathStore.hpp"
#include "../config/StageConfig.hpp"
#include "../dev/DevDiagDump.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;


int resolveRadius(int bakeRadius, BakeMethod method) {
    if (bakeRadius > 0) return bakeRadius;
    int base = StageConfig::ticksAll();
    if (method == BakeMethod::BruteForce) {
        return std::max(32, base * 4);
    }
    return std::max(8, base);
}

// After TPS remap, same-source-frame press+release can collapse to one bake
// frame. Push releases forward so every press is held ≥1 bake tick.
void ensureMinHoldTicks(std::vector<ReplayEvent>& ev, int minHold = 1) {
    if (minHold <= 0 || ev.empty()) return;
    for (size_t i = 0; i < ev.size(); ++i) {
        if (!ev[i].down || ev[i].button != 1) continue;
        for (size_t j = i + 1; j < ev.size(); ++j) {
            if (ev[j].button != ev[i].button) continue;
            if (ev[j].player2 != ev[i].player2) continue;
            if (ev[j].down) break; // next press
            if (ev[j].frame <= ev[i].frame) {
                ev[j].frame = ev[i].frame + minHold;
            }
            break;
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](auto const& a, auto const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        // Downs before ups on the same frame (matches bake apply order).
        return a.down && !b.down;
    });
}

// Physics-bypass macros (MegaHack 1000–3000): keep author quantum 1:1.
// Mild FPS (60/360) still snap to live ~240 — discrete collisions match the game.
double resolveBakeTps(double srcTps, double liveTps) {
    if (srcTps > AssistClock::kPlanSnapTpsHi) {
        return std::min(srcTps, 4000.0);
    }
    if (liveTps < AssistClock::kPlanSnapTpsLo || liveTps > AssistClock::kPlanSnapTpsHi) {
        return AssistClock::kNominalTps;
    }
    return liveTps;
}

// Scale a ~240-tick radius/horizon to author TPS wall-time.
int scaleBakeTicks(int ticks, double bakeTps) {
    if (ticks <= 0) return ticks;
    if (bakeTps <= AssistClock::kPlanSnapTpsHi) return ticks;
    long long scaled = std::llround(
        static_cast<double>(ticks) * (bakeTps / AssistClock::kNominalTps)
);
    if (scaled < ticks) scaled = ticks;
    if (scaled > 2000) scaled = 2000;
    return static_cast<int>(scaled);
}

// Remap author FPS → bake TPS (no-op when bakeTps == srcTps), min-hold, P1-only.
std::vector<ReplayEvent> prepareBakeTimeline(
    std::vector<ReplayEvent> const& src,
    double srcTps,
    double bakeTps,
    bool forceSoloP1
) {
    std::vector<ReplayEvent> ev = src;
    if (forceSoloP1) {
        // Keep real dual P2 only when P1 is majority; else MegaHack all-P2 → P1.
        int p1 = 0;
        int p2 = 0;
        for (auto const& e : ev) {
            if (e.player2) ++p2;
            else ++p1;
        }
        if (p2 >= p1) {
            for (auto& e : ev) e.player2 = false;
        }
        else {
            // Drop P2 dual clicks from P1 bake timeline (don't feed into ghost).
            ev.erase(
                std::remove_if(ev.begin(), ev.end(), [](ReplayEvent const& e) {
                    return e.player2;
                }),
                ev.end()
);
        }
    }
    if (srcTps > 0.0 && bakeTps > 0.0 && std::fabs(srcTps - bakeTps) >= 0.5) {
        float const srcDt = static_cast<float>(1.0 / srcTps);
        for (auto& e : ev) {
            e.frame = AssistClock::get().remapTick(bakeTps, e.frame, srcDt);
        }
    }
    // MegaHack GDR is often 1-based (first physics tick = frame 1). Shift so the
    // opening tap lands on sim frame 0 — otherwise frame 0 falls into spawn spikes.
    {
        int minF = std::numeric_limits<int>::max();
        for (auto const& e : ev) {
            if (e.frame < minF) minF = e.frame;
        }
        if (minF == 1) {
            for (auto& e : ev) e.frame -= 1;
        }
    }
    ensureMinHoldTicks(ev, 1);
    return ev;
}

unsigned workerCount() {
    // Main-thread only: Cocos/PlayLayer are not thread-safe.
    return 1u;
}


BakeMap& BakeMap::get() {
    static BakeMap inst;
    return inst;
}

void BakeMap::clear() {
    m_data = {};
    m_loaded = false;
    this->m_startXq = 0;
    this->m_dirty = false;
    RoutePathStore::get().clear();
}

void BakeMap::clearPendingReplay() {
    m_replay = {};
    m_hasReplay = false;
}

void BakeMap::setPendingReplay(ImportedReplay replay) {
    m_replay = std::move(replay);
    m_hasReplay = true;
    AssistLog::info(fmt::format(
        "BAKE: loaded replay {} ({} events, {:.0f} tps, {})",
        m_replay.sourceName, m_replay.events.size(), m_replay.tps, m_replay.format
));
}

void BakeMap::loadForLevel(GJGameLevel* level, int startXq) {
    m_data = {};
    m_loaded = false;
    this->m_dirty = false;
    this->m_startXq = startXq;
    if (!level) return;
    m_data.levelKey = LevelProfile::makeKey(level);
    // A route is an optional sibling of replay clicks. A missing/corrupt route
    // is deliberately a miss, never a reason to reject the click cache.
    (void)RoutePathStore::get().load(m_data.levelKey, startXq);
    auto res = readFile();
    if (res.isOk()) {
        // Empty click stores count as loaded so self-bake can merge from a cold start.
        m_loaded = true;
        if (!m_data.clicks.empty()) {
            AssistLog::debug(fmt::format(
                "BAKE: loaded map {} sp={} ({} clicks, {}, {})",
                m_data.levelKey,
                this->m_startXq,
                m_data.clicks.size(),
                m_data.sourceReplay,
                m_data.method
            ));
        }
        else {
            m_data.sourceReplay.clear();
            m_data.method.clear();
            m_data.tps = AssistClock::kNominalTps;
            AssistLog::debug(fmt::format(
                "BAKE: empty store ready {} sp={}",
                m_data.levelKey,
                this->m_startXq
            ));
        }
        return;
    }
    // Read error — keep levelKey; still mark loaded empty so self-bake works.
    AssistLog::debug(fmt::format(
        "BAKE: load miss {} sp={}: {}",
        m_data.levelKey,
        this->m_startXq,
        res.unwrapErr()
    ));
    m_data.sourceReplay.clear();
    m_data.method.clear();
    m_data.tps = AssistClock::kNominalTps;
    m_data.clicks.clear();
    m_loaded = true;
}

void BakeMap::setStartXq(int startXq) {
    if (this->m_startXq == startXq && this->m_loaded) return;
    if (this->m_data.levelKey.empty()) {
        this->m_startXq = startXq;
        return;
    }
    // Persist dirty clicks into the previous shard before switching.
    if (this->m_dirty && this->m_loaded) {
        this->save();
    }
    std::string const key = this->m_data.levelKey;
    bool const hadReplay = this->m_hasReplay;
    ImportedReplay replay = this->m_replay;
    this->m_data = {};
    this->m_data.levelKey = key;
    this->m_startXq = startXq;
    this->m_dirty = false;
    this->m_loaded = false;
    (void)RoutePathStore::get().load(key, startXq);
    auto res = this->readFile();
    this->m_loaded = true;
    if (res.isErr()) {
        this->m_data.sourceReplay.clear();
        this->m_data.method.clear();
        this->m_data.tps = AssistClock::kNominalTps;
        this->m_data.clicks.clear();
    }
    if (hadReplay) {
        this->m_replay = std::move(replay);
        this->m_hasReplay = true;
    }
    AssistLog::debug(fmt::format(
        "BAKE: startpos shard sp={} clicks={}",
        this->m_startXq,
        this->m_data.clicks.size()
    ));
}

void BakeMap::save() const {
    if (!m_loaded || m_data.levelKey.empty()) return;
    if (!this->m_dirty) return;
    auto res = writeFile();
    if (res.isOk()) {
        this->m_dirty = false;
        AssistLog::debug(fmt::format(
            "BAKE: saved {} ({} clicks, {})",
            m_data.levelKey, m_data.clicks.size(), m_data.method
));
    }
    else {
        AssistLog::debug(fmt::format("BAKE: save failed: {}", res.unwrapErr()));
    }
}

std::string BakeMap::statusLine() const {
    if (m_progress.running.load()) {
        return fmt::format(
            "baking {}/{} ({:.0f}%)",
            m_progress.done.load(),
            m_progress.total.load(),
            m_progress.fraction() * 100.f
);
    }
    if (m_hasReplay && !m_data.clicks.empty()) {
        return fmt::format(
            "{} | {} {} clicks",
            m_replay.sourceName, m_data.method, m_data.clicks.size()
);
    }
    if (m_hasReplay) return fmt::format("{} | no bake", m_replay.sourceName);
    if (!m_data.clicks.empty()) {
        return fmt::format(
            "{} {} clicks ({})",
            m_data.method, m_data.clicks.size(), m_data.sourceReplay
);
    }
    return "no bake";
}

std::string BakeMap::pauseStatusLine() const {
    std::string line = statusLine();
    if (m_hasReplay) {
        line = fmt::format(
            "Replay: {} ({} ev @ {:.0f} tps)\n{}",
            m_replay.sourceName, m_replay.events.size(), m_replay.tps, line
);
    }
    else {
        line = fmt::format(
            "Replay: (none) — Brute Bake needs none\n{}",
            line
);
    }
    return line;
}

int BakeMap::refineSelfWindows(
    PlayLayer* layer,
    int startSnapshotSlot,
    std::vector<BakedClick>& clicks,
    int bakeRadius,
    int budgetSims,
    std::atomic<bool>* cancel
) {
    if (!layer || startSnapshotSlot < 0 || clicks.empty() || budgetSims <= 0) return 0;
    if (isBaking()) return 0;

    bakeRadius = resolveRadius(bakeRadius, BakeMethod::Replay);
    double tps = m_data.tps > 0.0 ? m_data.tps : 240.0;
    float dt = static_cast<float>(1.0 / tps);
    int const horizon = ShadowSimulator::modelFor(
        layer->m_player1 ? layer->m_player1 : layer->m_player2,
        StageConfig::ticksAll()
).cap;

    std::stable_sort(clicks.begin(), clicks.end(), [](auto const& a, auto const& b) {
        if (a.nominalFrame != b.nominalFrame) return a.nominalFrame < b.nominalFrame;
        return a.player2 < b.player2;
    });

    std::vector<ReplayEvent> timeline;
    timeline.reserve(clicks.size());
    for (auto const& c : clicks) {
        timeline.push_back(ReplayEvent{ c.nominalFrame, 1, c.player2, c.down });
    }

    int simsLeft = budgetSims;
    int expanded = 0;
    auto cancelled = [&]() -> bool {
        return cancel && cancel->load();
    };
    auto spend = [&]() -> bool {
        if (simsLeft <= 0) return true;
        --simsLeft;
        return false;
    };

    for (size_t i = 0; i < clicks.size(); ++i) {
        if (cancelled() || simsLeft <= 0) break;

        auto& click = clicks[i];
        int const F = click.nominalFrame;
        int next = -1;
        for (size_t j = i + 1; j < clicks.size(); ++j) {
            if (clicks[j].player2 != click.player2) continue;
            next = clicks[j].nominalFrame;
            break;
        }
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);

        // Keep timeline event frames in sync with current click nominals.
        timeline[i].frame = F;

        std::lock_guard lock(m_simMutex);
        if (spend()) break;
        if (!testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(i), F, until, dt)) {
            // Nominal dies from start snapshot — keep point window.
            continue;
        }

        int lo = std::max(0, F - bakeRadius);
        int hi = F;
        while (lo < hi) {
            if (cancelled() || simsLeft <= 0) break;
            int mid = lo + (hi - lo) / 2;
            if (spend()) break;
            if (testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(i), mid, until, dt)) {
                hi = mid;
            }
            else {
                lo = mid + 1;
            }
        }
        int earliest = lo;

        lo = F;
        hi = F + bakeRadius;
        while (lo < hi) {
            if (cancelled() || simsLeft <= 0) break;
            int mid = lo + (hi - lo + 1) / 2;
            if (spend()) break;
            if (testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(i), mid, until, dt)) {
                lo = mid;
            }
            else {
                hi = mid - 1;
            }
        }
        int latest = lo;

        if (earliest > latest) std::swap(earliest, latest);
        if (earliest < click.earliest || latest > click.latest
            || earliest != click.nominalFrame || latest != click.nominalFrame) {
            if (earliest != click.earliest || latest != click.latest) ++expanded;
            click.earliest = std::min(click.earliest, earliest);
            click.latest = std::max(click.latest, latest);
        }
    }

    AssistLog::debug(fmt::format(
        "self-bake refine: {}/{} expanded, sims left {}",
        expanded, clicks.size(), simsLeft
));
    return expanded;
}

std::vector<BakedClick const*> BakeMap::nextClicksAfter(
    float playerX, bool down, bool player2,
    std::optional<GameModeKind> preferMode,
    int maxCount
) const {
    std::vector<BakedClick const*> out;
    if (maxCount <= 0 || !m_loaded) return out;

    auto collect = [&](bool requireMode) {
        std::vector<BakedClick const*> hits;
        for (auto const& c : m_data.clicks) {
            if (c.down != down || c.player2 != player2) continue;
            if (c.x <= playerX + 1.f) continue;
            if (requireMode && preferMode && c.mode != *preferMode) continue;
            hits.push_back(&c);
        }
        std::sort(hits.begin(), hits.end(), [](auto const* a, auto const* b) {
            if (a->x != b->x) return a->x < b->x;
            return a->nominalFrame < b->nominalFrame;
        });
        return hits;
    };

    auto hits = preferMode ? collect(true) : collect(false);
    if (hits.empty() && preferMode) hits = collect(false);
    for (auto const* c : hits) {
        out.push_back(c);
        if (static_cast<int>(out.size()) >= maxCount) break;
    }
    return out;
}

int BakeMap::mergeSelfClicks(std::vector<BakedClick> const& incoming) {
    if (incoming.empty() || !m_loaded) return 0;
    if (isBaking()) return 0;

    bool const wasEmpty = m_data.clicks.empty();
    int touched = 0;

    for (auto const& neu : incoming) {
        BakedClick* match = nullptr;
        for (auto& old : m_data.clicks) {
            if (old.down != neu.down || old.player2 != neu.player2) continue;
            if (std::fabs(old.x - neu.x) > MERGE_X_TOL) continue;
            if (std::abs(old.nominalFrame - neu.nominalFrame) > MERGE_FRAME_TOL) continue;
            match = &old;
            break;
        }
        if (match) {
            int const prevE = match->earliest;
            int const prevL = match->latest;
            bool const prevHold = match->holdAssist;
            match->earliest = std::min(match->earliest, neu.earliest);
            match->latest = std::max(match->latest, neu.latest);
            match->holdAssist = match->holdAssist || neu.holdAssist;
            if (match->earliest != prevE || match->latest != prevL
                || match->holdAssist != prevHold) {
                ++touched;
            }
            continue;
        }
        if (m_data.clicks.size() >= kMaxMapClicks) continue;
        m_data.clicks.push_back(neu);
        ++touched;
    }

    if (touched <= 0) return 0;

    if (wasEmpty || m_data.method == "self" || m_data.method.empty()) {
        m_data.method = "self";
        if (m_data.sourceReplay.empty()) m_data.sourceReplay = "self-bake";
    }
    // Do not sync-write here — death/reset flushSegment hit this on the main
    // thread and stalled restarts. Persist on quit / level complete / explicit save.
    this->m_dirty = true;
    return touched;
}

float BakeMap::windowHalfW(int earliest, int latest) {
    // Keep the drawn band tight — oversized boxes invited late presses that
    // still look "in window" after latest (salvage then dies).
    int const span = (std::max)(0, latest - earliest);
    return (std::clamp)(2.5f + static_cast<float>(span) * 0.28f, 2.5f, 16.f);
}

BakedClick const* BakeMap::findNear(
    float playerX, int approxFrame, bool down, bool player2,
    std::optional<GameModeKind> preferMode
) const {
    // Temporal gate: do not pick a window whose absolute bake frames are far
    // from approxFrame. LOOKUP_X_TOL alone can match ~100f early at cube speed
    // and the old relative frameToTick then treated that as "in window".
    static constexpr int kFramePastSlop = 240;
    static constexpr int kFrameFutureSlop = 48; // slightly before earliest OK

    // Prefer clicks whose drawn band contains playerX (viz overlap), else nearest X.
    // Among temporal candidates, prefer those with approxFrame inside [earliest, latest].
    auto pick = [&](bool requireMode) -> BakedClick const* {
        BakedClick const* best = nullptr;
        float bestScore = 1e9f;
        for (auto const& c : m_data.clicks) {
            if (c.down != down || c.player2 != player2) continue;
            if (requireMode && preferMode && c.mode != *preferMode) continue;
            float const dx = std::fabs(c.x - playerX);
            if (dx > LOOKUP_X_TOL) continue;
            if (approxFrame >= 0) {
                if (c.latest + kFramePastSlop < approxFrame) continue; // long past
                if (approxFrame + kFrameFutureSlop < c.earliest) continue; // too early
            }
            float const halfW = windowHalfW(c.earliest, c.latest);
            bool const inBand = dx <= halfW;
            bool const inTime = approxFrame < 0
                || (approxFrame >= c.earliest && approxFrame <= c.latest);
            // Score: in-time+in-band best, then in-time, then in-band, then dx.
            float score = dx;
            if (!inBand) score += LOOKUP_X_TOL;
            if (!inTime) score += LOOKUP_X_TOL * 2.f;
            if (score < bestScore) {
                bestScore = score;
                best = &c;
            }
        }
        return best;
    };

    if (preferMode) {
        if (auto const* hit = pick(true)) return hit;
    }
    return pick(false);
}

BakedClick const* BakeMap::recordedInputAt(int frame, bool down, bool player2) const {
    BakedClick const* nearest = nullptr;
    for (auto const& c : m_data.clicks) {
        if (!c.replayTiming || c.down != down || c.player2 != player2) continue;
        if (frame < c.earliest || frame > c.latest) continue;
        if (!nearest || std::abs(frame - c.nominalFrame) < std::abs(frame - nearest->nominalFrame)) {
            nearest = &c;
        }
    }
    return nearest;
}

bool BakeMap::testShiftedClick(
    PlayLayer* layer,
    int startSnapshotSlot,
    std::vector<ReplayEvent> timeline,
    int eventIndex,
    int testFrame,
    int untilFrame,
    float dt,
    int substeps,
    int minSurviveFrame,
    int noDeathBeforeFrame
) const {
    if (eventIndex < 0 || eventIndex >= static_cast<int>(timeline.size())) return false;
    timeline[static_cast<size_t>(eventIndex)].frame = testFrame;
    std::stable_sort(timeline.begin(), timeline.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    // Need to reach the 1:1 death frame to compare; otherwise a short until
    // can false-pass a shift that would have died earlier than validate.
    int runUntil = untilFrame;
    if (minSurviveFrame >= 0) {
        runUntil = std::max(runUntil, minSurviveFrame);
    }
    auto& sim = ShadowSimulator::get();
    // Reach clicks after a mid-macro 1:1 divergence by ignoring pre-click deaths.
    int const gate = noDeathBeforeFrame >= 0 ? noDeathBeforeFrame : testFrame;
    sim.setBakeNoDeathBeforeFrame(gate);
    auto r = sim.runBakeTimeline(
        layer, startSnapshotSlot, timeline, dt, runUntil, -1,
        true, false, true, substeps
);
    sim.setBakeNoDeathBeforeFrame(-1);
    if (!r.died) return true;
    // Prefix clicks: dying at/after the validate death matches 1:1.
    if (minSurviveFrame >= 0 && r.deathFrame >= minSurviveFrame) return true;
    return false;
}

Result<int> BakeMap::bake(
    PlayLayer* layer, int startSnapshotSlot, int bakeRadius, BakeMethod method
) {
    if (m_progress.running.exchange(true)) {
        return Err("Bake already running");
    }
    m_progress.cancel = false;
    m_progress.done = 0;
    m_progress.total = 0;
    m_progress.setStatus("starting…");

    auto finish = [&](Result<int> r) -> Result<int> {
        m_progress.running = false;
        if (r.isErr()) m_progress.setStatus(fmt::format("failed: {}", r.unwrapErr()));
        else if (method == BakeMethod::BruteForce) {
            m_progress.setStatus(fmt::format("done: {} clicks", r.unwrap()));
        }
        return r;
    };

    if (method == BakeMethod::BruteForce) {
        return finish(bakeBrute(layer, startSnapshotSlot, bakeRadius));
    }
    return finish(bakeReplay(layer, startSnapshotSlot, bakeRadius));
}

Result<int> BakeMap::bakeReplay(
    PlayLayer* layer, int startSnapshotSlot, int bakeRadius
) {
    if (!layer) return Err("No PlayLayer");
    if (!m_hasReplay || m_replay.events.empty()) return Err("No replay loaded");
    if (startSnapshotSlot < 0) return Err("No start snapshot (reset once, then bake)");

    bakeRadius = resolveRadius(bakeRadius, BakeMethod::Replay);

    // World must match start snapshot: bake right after reset, not mid-run.
    // Mid-level orb claims / toggles make the ghost die immediately at start.
    int const attemptTick = AssistController::get().attemptTick();
    if (attemptTick > 60) {
        AssistLog::info(fmt::format(
            "BAKE warn: attemptTick={} — prefer reset→pause→bake (mid-run world pollutes orbs/toggles)",
            attemptTick
));
    }
    // Spawn probe: hazards/solids already overlapping the start-snap ghost.
    auto const spawnProbe = BakeSpawnProbe::probe(layer, startSnapshotSlot);
    AssistLog::info(fmt::format(
        "BAKE: spawnProbe overlaps={} tracked={} recommendsTW={} snap=({:.1f},{:.1f}) dart={}",
        spawnProbe.overlapCount,
        spawnProbe.trackedOverlapCount,
        spawnProbe.recommendsTriggerWorld ? 1 : 0,
        spawnProbe.snapX,
        spawnProbe.snapY,
        spawnProbe.snapIsDart ? 1 : 0
));
    if (!spawnProbe.overlaps.empty()) {
        auto const& h0 = spawnProbe.overlaps.front();
        AssistLog::info(fmt::format(
            "BAKE: spawnHit0 uid={} objId={} type={} @({:.1f},{:.1f}) tw={} keys={}",
            h0.uniqueId, h0.objectId, h0.objectType, h0.x, h0.y,
            h0.inTriggerWorld ? 1 : 0, h0.twKeyCount
));
    }

    // Never validate while TW is still building / stale-empty when setting on.
    // Also force TW for this bake when spawn already overlaps a lethal object
    // (movers parked on spawn with Trigger World setting off — e.g. Grief).
    bool const forceTwForSpawn =
        spawnProbe.recommendsTriggerWorld && !StageConfig::triggerBake();
    if (StageConfig::triggerBake() || forceTwForSpawn) {
        m_progress.setStatus(
            forceTwForSpawn ? "Trigger World (spawn movers)…" : "waiting Trigger World…"
);
        std::string twErr;
        if (!TriggerWorldBaker::get().waitUntilReady(
                layer, 45000, twErr, forceTwForSpawn
)) {
            return Err(twErr.empty() ? "Trigger World not ready for bake" : twErr);
        }
        auto& tw = TriggerWorldStore::get();
        AssistLog::info(fmt::format(
            "BAKE: TW ready tracks={} ready={} softcap={} forceSpawn={} ({})",
            tw.trackCount(),
            tw.readyTrackCount(),
            tw.softcapHit() ? 1 : 0,
            forceTwForSpawn ? 1 : 0,
            TriggerWorldBaker::get().statusLine()
));
        if (tw.softcapHit()) {
            AssistLog::info(fmt::format(
                "BAKE warn: Trigger World softcap ({} tracks) — movers may be incomplete",
                tw.trackCount()
));
        }
        // Re-probe TW membership after build.
        auto const spawnAfter = BakeSpawnProbe::probe(layer, startSnapshotSlot);
        if (spawnAfter.trackedOverlapCount > 0 || tw.hasTracks()) {
            ShadowSimulator::get().setBakeForceTriggerWorld(true);
            AssistLog::info(fmt::format(
                "BAKE: bakeForceTriggerWorld=1 (trackedOverlaps={} tracks={})",
                spawnAfter.trackedOverlapCount,
                tw.trackCount()
));
        }
        else if (forceTwForSpawn) {
            AssistLog::info(
                "BAKE warn: spawn overlap but no TW tracks for those uids — "
                "frame-0 death may remain (static spike or unmodeled mover)"
);
        }
    }

    class BakeTwForceGuard {
    public:
        ~BakeTwForceGuard() {
            ShadowSimulator::get().setBakeForceTriggerWorld(false);
        }
    };
    BakeTwForceGuard twForceGuard;

    // Mild FPS (60/360) → resample onto live ~240. Physics-bypass (1000–3000)
    // stays 1:1 at author TPS so the ghost matches the bot quantum exactly.
    double const srcTps = m_replay.tps > 0.0 ? m_replay.tps : AssistClock::kNominalTps;
    double const liveTps = AssistController::get().estimatedTps();
    double const tps = resolveBakeTps(srcTps, liveTps);
    float const dt = static_cast<float>(1.0 / tps);
    bakeRadius = scaleBakeTicks(bakeRadius, tps);

    std::vector<ReplayEvent> ev = prepareBakeTimeline(m_replay.events, srcTps, tps, true);
    AssistLog::info(fmt::format(
        "BAKE replay: timeline {} events @ {:.0f}←{:.0f} tps radius={} "
        "(validate 1:1 then expand windows)",
        ev.size(), tps, srcTps, bakeRadius
));

    BakeMapData out;
    out.levelKey = m_data.levelKey.empty() && layer->m_level
        ? LevelProfile::makeKey(layer->m_level)
        : m_data.levelKey;
    if (out.levelKey.empty()) out.levelKey = "unknown";
    out.sourceReplay = m_replay.sourceName;
    out.tps = tps;
    out.method = "replay";

    bool const doRelease = StageConfig::assistRelease();

    auto const countedInput = [doRelease](ReplayEvent const& e) {
        return e.button == 1 && (e.down || doRelease);
    };
    int const numInputs = static_cast<int>(std::count_if(ev.begin(), ev.end(), countedInput));
    if (numInputs == 0) return Err("Replay has no enabled jump inputs to bake");
    if (ev.front().frame < 0 || ev.back().frame >= std::numeric_limits<int>::max() - std::max(numInputs, bakeRadius)) {
        return Err("Replay frame range exceeds the supported clock");
    }
    int const lastFrame = ev.back().frame;
    // Progress: validate frames + per-click expand.
    int const validateWeight = std::max(1, lastFrame);
    m_progress.total = validateWeight + numInputs;
    m_progress.done = 0;
    m_progress.setStatus(fmt::format(
        "validate macro 1:1 · {}f @ {:.0f} tps", lastFrame, tps
));
    AssistLog::info(fmt::format(
        "BAKE replay: validate 0..{} then expand {} clicks, radius {} @ {:.0f} tps",
        lastFrame, numInputs, bakeRadius, tps
));

    // Parity Gate: Soft portals Include + freeze ε before validate
    // (portal Include must not retune noise).
    {
        std::string portalDetail;
        if (!Tg2BakeFixture::bakeOfflinePortalsIncludeOk(portalDetail)) {
            return Err(fmt::format(
                "Bake portal policy failed ({}) — Soft portals must Include",
                portalDetail.empty() ? "?" : portalDetail
));
        }
        std::string parityDetail;
        AssistContracts140::applyLockedEps();
        if (!Tg2BakeFixture::parityGateOk(parityDetail)) {
            return Err(fmt::format(
                "Bake parity gate failed ({}) — ε LOCK required before validate",
                parityDetail.empty() ? "?" : parityDetail
));
        }
        AssistLog::debug("BAKE: parity gate OK (eps locked; Soft portals Include)");
    }

    // Start pose (StartPos / normal). Snapshot X tells Hybrid parts where bake begins.
    bool const startPosAttempt = layer->m_isTestMode;
    float startX = 0.f;
    if (auto* snap = ShadowSimulator::get().snapshotSlot(startSnapshotSlot)) {
        startX = snap->getPositionX();
    }
    this->setStartXq(LevelFingerprint::startXq(startX));
    AssistLog::info(fmt::format(
        "BAKE: startPose startPos={} startX={:.1f} sp={} attemptTick={} twTracks={}",
        startPosAttempt ? 1 : 0,
        startX,
        this->m_startXq,
        attemptTick,
        TriggerWorldStore::get().trackCount()
    ));

    // One continuous playback supplies positions for every recorded edge.
    ShadowSimulator::BakeRunResult validate;
    {
        std::lock_guard lock(m_simMutex);
        int lastLogged = -1;
        validate = ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, ev, dt, lastFrame, -1,
            true, true, true, 1,
            [&](int frame) -> bool {
                if (m_progress.cancel.load()) return false;
                if (frame - lastLogged >= 64 || frame == lastFrame) {
                    lastLogged = frame;
                    m_progress.done = frame;
                    m_progress.setStatus(fmt::format(
                        "validate {}/{}", frame, lastFrame
));
                }
                return true;
            }, ShadowSimulator::BakePlayback::FollowReplay
        );
    }
    if (m_progress.cancel.load()) return Err("Bake cancelled");

    // The replay supplies the timing. The ghost supplies a position for each
    // input and can report several consecutive contacts with the same obstacle.
    int issueSections = 0;
    bool inIssue = false;
    for (auto const& pose : validate.frames) {
        bool const issue = pose.dead || pose.unknown;
        if (issue && !inIssue) ++issueSections;
        inIssue = issue;
    }
    bool const incomplete = validate.frames.size() <= static_cast<size_t>(lastFrame);
    if (incomplete && !inIssue) ++issueSections;

    m_progress.done = validateWeight;
    for (auto const& e : ev) {
        if (m_progress.cancel.load()) return Err("Bake cancelled");
        if (!countedInput(e)) continue;
        ++m_progress.done;
        if (static_cast<size_t>(e.frame) >= validate.frames.size()) continue;
        auto const& pose = validate.frames[e.frame];
        BakedClick c;
        c.nominalFrame = e.frame;
        c.earliest = std::max(0, e.frame - bakeRadius);
        c.latest = e.frame + bakeRadius;
        c.down = e.down;
        c.player2 = e.player2;
        c.x = pose.x;
        c.mode = static_cast<GameModeKind>(pose.mode);
        c.holdAssist = !e.down && pose.dashing;
        c.replayTiming = true;
        out.clicks.push_back(c);
    }
    int const nClicks = static_cast<int>(out.clicks.size());
    auto summary = fmt::format("{} of {} inputs saved. {} skipped. Ghost issue sections: {}.",
        nClicks, numInputs, numInputs - nClicks, issueSections);
    if (nClicks == 0) return Err(summary);

    // Publish the replay windows only after all clicks have been considered.
    auto write = ClickBakeStore::saveMethod(out, m_startXq);
    if (write.isErr()) return Err(fmt::format("Could not save replay windows: {}", write.unwrapErr()));
    m_data = std::move(out);
    m_loaded = true;
    m_dirty = false;

    auto route = RoutePathBuilder::buildFromTraces(validate.frames);
    if (issueSections > 0) {
        route.source = RoutePath::kReplayPattern;
        route.parityOk = false;
    }
    if (!route.empty()) {
        auto saved = RoutePathStore::get().save(m_data.levelKey, route, m_startXq);
        if (saved.isErr()) return Err(fmt::format("{} Route write failed: {}", summary, saved.unwrapErr()));
    }
    else {
        std::string err;
        if (!RoutePathStore::removeFile(m_data.levelKey, m_startXq, err)) {
            return Err(fmt::format("{} Could not clear previous route: {}", summary, err));
        }
        summary += " Timing only.";
    }
    m_progress.setStatus(summary);
    AssistLog::info(fmt::format("BAKE replay {}: {} First ghost contact: {}. Route samples: {}.",
        m_data.sourceReplay, summary, validate.deathFrame, route.sampleCount()));
    return Ok(nClicks);
}

Result<int> BakeMap::bakeBrute(
    PlayLayer* layer, int startSnapshotSlot, int bakeRadius
) {
    // Pure brute: no replay. Drive forward from start; at each death, exhaustively
    // try press/release timings in [death-radius, death] and keep windows that
    // don't die (progress further). Parallel job queue; GD physics mutexed.
    if (!layer) return Err("No PlayLayer");
    if (startSnapshotSlot < 0) return Err("No start snapshot (reset once, then Brute Bake)");
    if (!layer->m_player1) return Err("No player");

    bakeRadius = resolveRadius(bakeRadius, BakeMethod::BruteForce);
    // Same physics quantum as live plan (AssistClock / estimatedTps = 1/simDt).
    double tps = AssistController::get().estimatedTps();
    if (tps < 60.0 || tps > 480.0) tps = 240.0;
    float const dt = static_cast<float>(1.0 / tps);
    int const maxFrames = static_cast<int>(tps * 180.0); // 3 min physics cap
    int const maxClicks = 400;
    int const horizon = ShadowSimulator::modelFor(
        layer->m_player1, StageConfig::ticksAll()
    ).cap;
    bool const doRelease = StageConfig::assistRelease();
    auto const model = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll());

    float startX = 0.f;
    if (auto* snap = ShadowSimulator::get().snapshotSlot(startSnapshotSlot)) {
        startX = snap->getPositionX();
    }
    this->setStartXq(LevelFingerprint::startXq(startX));

    BakeMapData out;
    out.levelKey = m_data.levelKey.empty() && layer->m_level
        ? LevelProfile::makeKey(layer->m_level)
        : m_data.levelKey;
    if (out.levelKey.empty()) out.levelKey = "unknown";
    out.sourceReplay = "(brute)";
    out.tps = tps;
    out.method = "brute";

    m_progress.total = maxClicks;
    m_progress.done = 0;
    m_progress.setStatus(fmt::format(
        "brute (no replay) · radius {} · serial @ {:.0f} tps", bakeRadius, tps
));
    AssistLog::info(fmt::format(
        "BAKE brute: death-driven search, radius {}, horizon {}, serial main-thread @ {:.0f} tps",
        bakeRadius, horizon, tps
));

    auto holdingAtEnd = [](std::vector<ReplayEvent> const& tl) {
        bool held = false;
        for (auto const& e : tl) {
            if (e.button == 1) held = e.down;
        }
        return held;
    };

    auto runUntil = [&](std::vector<ReplayEvent> const& tl, int until, int sampleFrame)
        -> ShadowSimulator::BakeRunResult {
        std::lock_guard lock(m_simMutex);
        return ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, tl, dt, until, sampleFrame
);
    };

    struct Trial {
        int actionFrame = 0;
        bool down = true;
        int releaseFrame = -1; // optional follow-up release
        int surviveUntil = -1; // death frame, or until if survived
        bool survived = false;
        float x = 0.f;
        GameModeKind mode = GameModeKind::Cube;
    };

    std::vector<ReplayEvent> committed;
    int lastDeath = -1;
    int stuck = 0;

    for (int step = 0; step < maxClicks; ++step) {
        if (m_progress.cancel.load()) return Err("Bake cancelled");

        auto baseline = runUntil(committed, maxFrames, -1);
        if (!baseline.died) {
            m_progress.setStatus(fmt::format(
                "brute done — survived {}f with {} clicks",
                maxFrames, out.clicks.size()
));
            AssistLog::info(fmt::format(
                "BAKE brute: cleared horizon ({} frames) with {} clicks",
                maxFrames, out.clicks.size()
));
            break;
        }

        int const D = baseline.deathFrame;
        if (D < 0) break;
        if (D == lastDeath) {
            if (++stuck >= 2) {
                AssistLog::info(fmt::format(
                    "BAKE brute: stuck at death frame {} — stopping ({} clicks so far)",
                    D, out.clicks.size()
));
                break;
            }
        }
        else {
            stuck = 0;
            lastDeath = D;
        }

        bool const held = holdingAtEnd(committed);
        int const lo = std::max(0, D - bakeRadius);
        int const hi = D;
        int const untilProbe = std::min(maxFrames, D + std::max(horizon, 16));

        // Build candidate action list: (frame, down, optional release)
        struct ActionSpec {
            int frame;
            bool down;
            int releaseFrame;
        };
        std::vector<ActionSpec> specs;
        specs.reserve(static_cast<size_t>((hi - lo + 1) * 4));

        for (int t = lo; t <= hi; ++t) {
            if (!held) {
                // Press (hold)
                specs.push_back({ t, true, -1 });
                // Tap variants for cube-like modes
                if (model.tapRelease || !model.continuous) {
                    for (int rel : { 1, 2, 3, 4, 6, 8 }) {
                        specs.push_back({ t, true, t + rel });
                    }
                }
            }
            if (held || doRelease) {
                // Release if currently holding (or always probe release for assist-release)
                if (held) {
                    specs.push_back({ t, false, -1 });
                }
            }
        }

        if (specs.empty()) {
            AssistLog::info(fmt::format("BAKE brute: no actions to try at death {}", D));
            break;
        }

        m_progress.setStatus(fmt::format(
            "brute death @{} · probing {} combos · clicks {}",
            D, specs.size(), out.clicks.size()
));

        std::vector<Trial> trials(specs.size());

        auto processSpec = [&](size_t idx) {
            if (m_progress.cancel.load()) return;
            auto const& sp = specs[idx];
            auto tl = committed;
            tl.push_back(ReplayEvent{ sp.frame, 1, false, sp.down });
            if (sp.releaseFrame >= 0) {
                tl.push_back(ReplayEvent{ sp.releaseFrame, 1, false, false });
            }
            std::stable_sort(tl.begin(), tl.end(), [](auto const& a, auto const& b) {
                return a.frame < b.frame;
            });

            Trial tr;
            tr.actionFrame = sp.frame;
            tr.down = sp.down;
            tr.releaseFrame = sp.releaseFrame;

            {
                std::lock_guard lock(m_simMutex);
                if (m_progress.cancel.load()) return;
                auto sample = ShadowSimulator::get().runBakeTimeline(
                    layer, startSnapshotSlot, tl, dt, sp.frame, sp.frame
);
                tr.x = sample.sampled ? sample.xAtSample : 0.f;
                tr.mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

                auto r = ShadowSimulator::get().runBakeTimeline(
                    layer, startSnapshotSlot, tl, dt, untilProbe, -1
);
                if (r.died) {
                    tr.surviveUntil = r.deathFrame;
                    tr.survived = r.deathFrame > D;
                }
                else {
                    tr.surviveUntil = untilProbe;
                    tr.survived = true;
                }
            }
            trials[idx] = tr;
            m_progress.done.fetch_add(1);
        };

        // Inflate total roughly so the % bar moves during probes
        m_progress.total = std::max(
            m_progress.total.load(),
            m_progress.done.load() + static_cast<int>(specs.size()) + (maxClicks - step)
);

        // Serial main-thread only (Cocos/PlayLayer are not thread-safe).
        for (size_t idx = 0; idx < specs.size(); ++idx) {
            if (m_progress.cancel.load()) return Err("Bake cancelled");
            processSpec(idx);
        }
        if (m_progress.cancel.load()) return Err("Bake cancelled");

        // Best survival past current death
        int bestUntil = D;
        for (auto const& tr : trials) {
            if (tr.surviveUntil > bestUntil) bestUntil = tr.surviveUntil;
        }
        if (bestUntil <= D) {
            AssistLog::info(fmt::format(
                "BAKE brute: no combo survives past death {} — stopping", D
));
            break;
        }

        // Prefer press-downs that reach bestUntil (within 2 frames slack)
        int const slack = 2;
        std::vector<Trial const*> winners;
        for (auto const& tr : trials) {
            if (tr.surviveUntil + slack >= bestUntil && tr.surviveUntil > D) {
                winners.push_back(&tr);
            }
        }
        if (winners.empty()) break;

        // Split by down vs release — prefer whichever group has more / better
        Trial const* pick = winners.front();
        int earliest = pick->actionFrame;
        int latest = pick->actionFrame;
        bool pickDown = pick->down;
        int pickRel = pick->releaseFrame;

        // Prefer down actions if any winner is a down
        bool anyDown = false;
        for (auto* w : winners) {
            if (w->down) { anyDown = true; break; }
        }
        earliest = std::numeric_limits<int>::max();
        latest = -1;
        for (auto* w : winners) {
            if (anyDown && !w->down) continue;
            if (w->actionFrame < earliest) {
                earliest = w->actionFrame;
                pick = w;
                pickDown = w->down;
                pickRel = w->releaseFrame;
            }
            if (w->actionFrame > latest) latest = w->actionFrame;
        }
        if (latest < earliest) break;

        int const nominal = pick->actionFrame;
        float const x = pick->x;
        auto const mode = pick->mode;

        BakedClick c;
        c.nominalFrame = nominal;
        c.earliest = earliest;
        c.latest = latest;
        c.down = pickDown;
        c.player2 = false;
        c.x = x;
        c.mode = mode;
        c.holdAssist = false;
        out.clicks.push_back(c);

        // Also bake release window if we committed a tap-release
        if (pickRel >= 0 && doRelease) {
            BakedClick rel;
            rel.nominalFrame = pickRel;
            rel.earliest = pickRel;
            rel.latest = pickRel;
            rel.down = false;
            rel.player2 = false;
            rel.x = x;
            rel.mode = mode;
            // Probe dash while press is held through the release frame.
            {
                auto holdTl = committed;
                holdTl.push_back(ReplayEvent{ nominal, 1, false, true });
                auto dashProbe = runUntil(holdTl, pickRel, pickRel);
                rel.holdAssist = dashProbe.sampled && dashProbe.dashingAtSample;
            }
            out.clicks.push_back(rel);
        }

        committed.push_back(ReplayEvent{ nominal, 1, false, pickDown });
        if (pickRel >= 0) {
            committed.push_back(ReplayEvent{ pickRel, 1, false, false });
        }
        std::stable_sort(committed.begin(), committed.end(), [](auto const& a, auto const& b) {
            return a.frame < b.frame;
        });

        AssistLog::debug(fmt::format(
            "BAKE brute: {} @{} window [{},{}] survives→{} (was death {})",
            pickDown ? "press" : "release", nominal, earliest, latest, bestUntil, D
));
        m_progress.setStatus(fmt::format(
            "brute click #{} · {} @{} [{},{}] · next death hunt",
            out.clicks.size(), pickDown ? "press" : "release",
            nominal, earliest, latest
));
    }

    int const nClicks = static_cast<int>(out.clicks.size());
    AssistLog::info(fmt::format(
        "BAKE done clicks={} frames_cap={} tps={:.0f} (brute)",
        nClicks, maxFrames, tps
));

    if (nClicks <= 0) {
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(fmt::format(
                    "BAKE brute: 0 windows — wrote diag bundle {}", diagDir.string()
));
            }
        }
        return Err(
            "Brute found no safe clicks (died immediately / unsolvable from start). "
            "Reset once, then Brute Bake. Diag under save dir /diag/bakefail_*."
);
    }

    m_data = std::move(out);
    m_loaded = true;
    this->m_dirty = true;
    save();
    return Ok(nClicks);
}

Result<> BakeMap::writeFile() const {
    return ClickBakeStore::saveMethod(m_data, this->m_startXq);
}

Result<> BakeMap::readFile() {
    std::string const key = m_data.levelKey;
    auto res = ClickBakeStore::loadMerged(key, m_data, this->m_startXq);
    if (res.isErr()) return res;
    m_data.levelKey = key;
    return Ok();
}
