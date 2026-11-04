#include "BakeMap.hpp"
#include "AssistClock.hpp"
#include "AssistContracts140.hpp"
#include "AssistController.hpp"
#include "BakeSpawnProbe.hpp"
#include "ShadowSimulator.hpp"
#include "Tg2BakeFixture.hpp"
#include "TriggerWorldBaker.hpp"
#include "TriggerWorldStore.hpp"
#include "LevelProfile.hpp"
#include "../config/StageConfig.hpp"
#include "../dev/DevDiagDump.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;



std::filesystem::path bakesDir() {
    return Mod::get()->getSaveDir() / "bakes";
}

std::filesystem::path bakePath(std::string const& key) {
    return bakesDir() / (key + ".json");
}

int nextJumpFrame(std::vector<ReplayEvent> const& ev, int fromIndex, bool player2) {
    for (int i = fromIndex + 1; i < static_cast<int>(ev.size()); ++i) {
        if (ev[i].button != 1) continue;
        if (ev[i].player2 != player2) continue;
        if (ev[i].down) return ev[i].frame;
    }
    return -1;
}

struct JumpJob {
    int eventIndex = 0;
    int frame = 0;
    bool down = true;
    bool player2 = false;
    int until = 0;
};

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
    this->m_dirty = false;
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

void BakeMap::loadForLevel(GJGameLevel* level) {
    m_data = {};
    m_loaded = false;
    this->m_dirty = false;
    if (!level) return;
    m_data.levelKey = LevelProfile::makeKey(level);
    auto res = readFile();
    if (res.isOk() && !m_data.clicks.empty()) {
        m_loaded = true;
        AssistLog::debug(fmt::format(
            "BAKE: loaded map {} ({} clicks, {}, {})",
            m_data.levelKey, m_data.clicks.size(), m_data.sourceReplay, m_data.method
        ));
        return;
    }
    // Empty / missing bake file — keep levelKey for next save, not a loaded map.
    m_data.sourceReplay.clear();
    m_data.method.clear();
    m_data.tps = AssistClock::kNominalTps;
    m_data.clicks.clear();
    m_loaded = false;
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
        m_progress.setStatus(r.isOk()
            ? fmt::format("done: {} clicks", r.unwrap())
            : fmt::format("failed: {}", r.unwrapErr()));
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

    std::vector<JumpJob> jobs;
    for (int i = 0; i < static_cast<int>(ev.size()); ++i) {
        auto const& e = ev[static_cast<size_t>(i)];
        if (e.button != 1) continue;
        if (!e.down && !doRelease) continue;
        int F = e.frame;
        int next = nextJumpFrame(ev, i, e.player2);
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        if (horizon <= 0) horizon = std::max(16, StageConfig::ticksAll() * 4);
        horizon = scaleBakeTicks(horizon, tps);
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);
        jobs.push_back(JumpJob{ i, F, e.down, e.player2, until });
    }
    if (jobs.empty()) {
        return Err("Replay has no Jump events to bake (import a macro with clicks)");
    }

    int lastFrame = 0;
    for (auto const& e : ev) {
        if (e.frame > lastFrame) lastFrame = e.frame;
    }
    // Progress: validate frames + per-click expand.
    int const validateWeight = std::max(1, lastFrame);
    m_progress.total = validateWeight + static_cast<int>(jobs.size());
    m_progress.done = 0;
    m_progress.setStatus(fmt::format(
        "validate macro 1:1 · {}f @ {:.0f} tps", lastFrame, tps
    ));
    AssistLog::info(fmt::format(
        "BAKE replay: validate 0..{} then expand {} clicks, radius {} @ {:.0f} tps",
        lastFrame, jobs.size(), bakeRadius, tps
    ));

    // Commit 6 Parity Gate: Soft portals Include + freeze ε before validate
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
    AssistLog::info(fmt::format(
        "BAKE: startPose startPos={} startX={:.1f} attemptTick={} twTracks={}",
        startPosAttempt ? 1 : 0,
        startX,
        attemptTick,
        TriggerWorldStore::get().trackCount()
    ));

    // ── Validate 1:1 from start snapshot. Death ≠ hard fail: keep the
    // surviving prefix (Hybrid often only needs a StartPos part, not the
    // whole macro). Only frame-0 (no prefix) still errors.
    ShadowSimulator::BakeRunResult validate;
    {
        std::lock_guard lock(m_simMutex);
        int lastLogged = -1;
        validate = ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, ev, dt, lastFrame, -1,
            true, false, true, 1,
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
            }
        );
    }
    if (m_progress.cancel.load()) return Err("Bake cancelled");

    int partialCut = -1; // frames >= cut are dropped; -1 = full macro OK
    if (validate.died) {
        // P2 drop stats for err / log (same policy as prepareBakeTimeline).
        int droppedP2 = 0;
        {
            int p1 = 0;
            int p2 = 0;
            for (auto const& e : m_replay.events) {
                if (e.player2) ++p2;
                else ++p1;
            }
            if (p2 < p1) droppedP2 = p2;
        }
        auto const top3 = DevDiagDump::formatUnknownTop3(validate.unknownHist);
        int const twTracks = TriggerWorldStore::get().trackCount();
        auto const residual = Tg2BakeFixture::formatResidual(
            twTracks,
            validate.deathFrame,
            validate.deathObjId,
            validate.deathObjType
        );
        AssistLog::info(fmt::format(
            "BAKE: macro dies in sim @ frame {} deathX={:.1f} deathY={:.1f} "
            "objId={} objType={} gameId={} twTracks={} droppedP2={} "
            "unknownTop3=[{}] soft={} hard={} residual={} startPos={} startX={:.1f}",
            validate.deathFrame, validate.deathX, validate.deathY,
            validate.deathObjId, validate.deathObjType, validate.deathObjGameId,
            twTracks, droppedP2, top3,
            validate.unknownSoftHits, validate.unknownHardHits,
            residual.empty() ? "-" : residual,
            startPosAttempt ? 1 : 0,
            startX
        ));
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(fmt::format(
                    "BAKE: validate death — wrote diag {}", diagDir.string()
                ));
            }
        }

        int const cut = validate.deathFrame;
        if (cut <= 0) {
            // No surviving prefix — cannot expand any click from this pose.
            auto const spawnFail = BakeSpawnProbe::probe(layer, startSnapshotSlot);
            std::string killerDetail;
            if (validate.deathObjId >= 0) {
                killerDetail = fmt::format(
                    " deathObjId={} type={} gameId={}",
                    validate.deathObjId, validate.deathObjType, validate.deathObjGameId
                );
            }
            if (!spawnFail.overlaps.empty()) {
                auto const& h = spawnFail.overlaps.front();
                killerDetail += fmt::format(
                    " spawnHazard uid={} objId={} @({:.0f},{:.0f}) twTracked={}/{}",
                    h.uniqueId, h.objectId, h.x, h.y,
                    spawnFail.trackedOverlapCount, spawnFail.overlapCount
                );
            }
            int firstJump = -1;
            bool firstJumpDown = false;
            for (auto const& e : m_replay.events) {
                if (e.button != 1) continue;
                firstJump = e.frame;
                firstJumpDown = e.down;
                break;
            }
            std::string err = fmt::format(
                "Macro dies in sim at frame {} (x={:.0f}) twTracks={} "
                "unknownTop3=[{}] droppedP2={} startPos={} startX={:.0f} "
                "snapDart={} jumpHeld={} events={} firstJump={}{}",
                validate.deathFrame, validate.deathX,
                twTracks, top3, droppedP2,
                startPosAttempt ? 1 : 0, startX,
                spawnFail.snapIsDart ? 1 : 0,
                spawnFail.jumpHeld ? 1 : 0,
                static_cast<int>(m_replay.events.size()),
                firstJump,
                killerDetail
            );
            if (!residual.empty()) {
                err += " — ";
                err += residual;
            }
            if (static_cast<int>(m_replay.events.size()) <= 2
                && firstJump > 16
                && (spawnFail.snapIsDart
                    || spawnFail.snapMode == static_cast<int>(GameModeKind::Ship)
                    || spawnFail.snapMode == static_cast<int>(GameModeKind::Swing))
                && !spawnFail.jumpHeld
                && firstJumpDown) {
                err += " — wave/ship/swing start needs hold from frame 0; this macro's "
                       "first click is late (sparse/stub export — grief20 was 1 input / "
                       "58 bytes). Re-export a full macro from attempt start.";
            }
            else if (twTracks <= 0 && spawnFail.overlapCount > 0) {
                err += " — spawn hazard with Trigger World empty; "
                       "enable Trigger World or reset→pause→Bake after TW builds.";
            }
            else if (startPosAttempt) {
                err += " — StartPos bake: reset on the StartPos, pause, bake; "
                       "macro frames must be from the same StartPos attempt.";
            }
            else {
                err += " — ghost path ≠ bot at frame 0. Reset→pause→Bake.";
            }
            err += " Diag: /diag/bakefail_*";
            return Err(err);
        }

        // 1:1 died — still expand EVERY macro click. Prefix uses minSurvive;
        // post-death clicks ghost through pre-click deaths to reach the event.
        partialCut = cut;
        for (auto& j : jobs) {
            if (j.frame < cut && j.until > cut) j.until = cut;
        }
        AssistLog::info(fmt::format(
            "BAKE: validate died @{} x={:.0f} — expanding all {} clicks "
            "(pre-click deaths ignored for post-cut events)",
            cut, validate.deathX, jobs.size()
        ));
    }

    m_progress.done = validateWeight;
    m_progress.setStatus(fmt::format(
        "expand windows · {} clicks · radius {}", jobs.size(), bakeRadius
    ));
    if (partialCut >= 0) {
        AssistLog::info(fmt::format(
            "BAKE: expanding {} click windows (validateDeath @{})",
            jobs.size(), partialCut
        ));
    }
    else {
        AssistLog::info(fmt::format(
            "BAKE: validate OK through frame {} — expanding {} click windows",
            lastFrame, jobs.size()
        ));
    }

    // ── Phase 2: binary-search earliest/latest around each click ───────
    // Every Jump event gets a window attempt. 1:1 death does not drop later jobs.
    for (auto const& job : jobs) {
        if (m_progress.cancel.load()) return Err("Bake cancelled");

        // Prefix (before validate death): must not die earlier than 1:1.
        // Later clicks: ignore deaths before the click so expand can reach it.
        bool const postCut = partialCut >= 0 && job.frame >= partialCut;
        int const minSurvive = postCut ? -1 : partialCut;
        int const noDeathBefore = job.frame;

        std::lock_guard lock(m_simMutex);
        auto& sim = ShadowSimulator::get();
        sim.setBakeNoDeathBeforeFrame(noDeathBefore);
        auto sample = sim.runBakeTimeline(
            layer, startSnapshotSlot, ev, dt, job.frame, job.frame
        );
        sim.setBakeNoDeathBeforeFrame(-1);
        float x = sample.sampled ? sample.xAtSample : 0.f;
        auto mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

        if (!testShiftedClick(
                layer, startSnapshotSlot, ev, job.eventIndex, job.frame, job.until, dt,
                1, minSurvive, noDeathBefore
            )) {
            AssistLog::debug(fmt::format(
                "BAKE: skip click @{} (expand re-check fail postCut={} minSurvive={})",
                job.frame, postCut ? 1 : 0, minSurvive
            ));
            m_progress.done.fetch_add(1);
            continue;
        }

        int lo = std::max(0, job.frame - bakeRadius);
        int hi = job.frame;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (testShiftedClick(
                    layer, startSnapshotSlot, ev, job.eventIndex, mid, job.until, dt,
                    1, minSurvive, mid
                )) {
                hi = mid;
            }
            else {
                lo = mid + 1;
            }
        }
        int earliest = lo;

        lo = job.frame;
        hi = job.frame + bakeRadius;
        while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            if (testShiftedClick(
                    layer, startSnapshotSlot, ev, job.eventIndex, mid, job.until, dt,
                    1, minSurvive, mid
                )) {
                lo = mid;
            }
            else {
                hi = mid - 1;
            }
        }
        int latest = lo;

        BakedClick c;
        c.nominalFrame = job.frame;
        c.earliest = earliest;
        c.latest = latest;
        c.down = job.down;
        c.player2 = job.player2;
        c.x = x;
        c.mode = mode;
        // Release while dashing → dash/hold-orb; purple viz + later release gate.
        c.holdAssist = !job.down && sample.sampled && sample.dashingAtSample;
        out.clicks.push_back(c);

        m_progress.done.fetch_add(1);
        m_progress.setStatus(fmt::format(
            "expand {}/{} · @{} → [{},{}]",
            static_cast<int>(out.clicks.size()), jobs.size(),
            job.frame, earliest, latest
        ));
    }

    int const nClicks = static_cast<int>(out.clicks.size());
    AssistLog::info(fmt::format(
        "BAKE done clicks={} jobs={} lastFrame={} tps={:.0f} source={} "
        "partialCut={} startPos={} (replay)",
        nClicks, jobs.size(), lastFrame, tps, out.sourceReplay,
        partialCut, startPosAttempt ? 1 : 0
    ));

    if (nClicks <= 0) {
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(fmt::format(
                    "BAKE: 0 windows after validate — diag {}", diagDir.string()
                ));
            }
        }
        return Err(fmt::format(
            "Replay bake produced 0 windows — expand failed "
            "(jobs={} validateDeath={}). Diag: /diag/bakefail_*",
            jobs.size(), partialCut
        ));
    }

    if (partialCut >= 0) {
        // Surface partial in progress status (scheduler success alert stays click count).
        m_progress.setStatus(fmt::format(
            "partial {} windows (cut @{})", nClicks, partialCut
        ));
    }

    m_data = std::move(out);
    m_loaded = true;
    this->m_dirty = true;
    save();
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
    (void)file::createDirectoryAll(bakesDir());
    matjson::Value root = matjson::Value::object();
    root["version"] = 1;
    root["key"] = m_data.levelKey;
    root["source"] = m_data.sourceReplay;
    root["tps"] = m_data.tps;
    root["method"] = m_data.method;

    std::vector<matjson::Value> arr;
    arr.reserve(m_data.clicks.size());
    for (auto const& c : m_data.clicks) {
        matjson::Value j = matjson::Value::object();
        j["nominal"] = c.nominalFrame;
        j["earliest"] = c.earliest;
        j["latest"] = c.latest;
        j["down"] = c.down;
        j["player2"] = c.player2;
        j["x"] = c.x;
        j["mode"] = StageConfig::kindName(c.mode);
        if (c.holdAssist) j["holdAssist"] = true;
        arr.push_back(std::move(j));
    }
    root["clicks"] = arr;
    return file::writeToJson(bakePath(m_data.levelKey), root);
}

Result<> BakeMap::readFile() {
    auto res = file::readJson(bakePath(m_data.levelKey));
    if (res.isErr()) return Err(res.unwrapErr());
    auto root = res.unwrap();

    if (root.contains("source")) m_data.sourceReplay = root["source"].asString().unwrapOr("");
    if (root.contains("tps")) m_data.tps = root["tps"].asDouble().unwrapOr(240.0);
    if (root.contains("method")) m_data.method = root["method"].asString().unwrapOr("replay");
    m_data.clicks.clear();
    if (!root.contains("clicks") || !root["clicks"].isArray()) return Ok();

    for (auto const& j : root["clicks"]) {
        BakedClick c;
        c.nominalFrame = static_cast<int>(j["nominal"].asInt().unwrapOr(0));
        c.earliest = static_cast<int>(j["earliest"].asInt().unwrapOr(c.nominalFrame));
        c.latest = static_cast<int>(j["latest"].asInt().unwrapOr(c.nominalFrame));
        c.down = j["down"].asBool().unwrapOr(true);
        c.player2 = j["player2"].asBool().unwrapOr(false);
        c.x = static_cast<float>(j["x"].asDouble().unwrapOr(0.0));
        c.holdAssist = j.contains("holdAssist")
            ? j["holdAssist"].asBool().unwrapOr(false)
            : false;
        auto modeName = j.contains("mode") ? j["mode"].asString().unwrapOr("cube") : "cube";
        c.mode = GameModeKind::Cube;
        for (int i = 0; i < StageConfig::MODE_COUNT; ++i) {
            if (modeName == StageConfig::kindName(static_cast<GameModeKind>(i))) {
                c.mode = static_cast<GameModeKind>(i);
                break;
            }
        }
        m_data.clicks.push_back(c);
    }
    return Ok();
}
