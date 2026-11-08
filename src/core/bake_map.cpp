#include "bake_map.hpp"
#include "../config/stage_config.hpp"
#include "../debug/assist_log.hpp"
#include "../dev/dev_diag_dump.hpp"
#include "assist_clock.hpp"
#include "assist_contracts140.hpp"
#include "assist_controller.hpp"
#include "level_profile.hpp"
#include "shadow_simulator.hpp"
#include "tg2_bake_fixture.hpp"
#include "trigger_world_baker.hpp"
#include "trigger_world_store.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

std::filesystem::path BakeMapImplementation::bakesDir() {
    return geode::prelude::Mod::get()->getSaveDir() / "bakes";
}

std::filesystem::path BakeMapImplementation::bakePath(std::string const& key) {
    return BakeMapImplementation::bakesDir() / (key + ".json");
}

int BakeMapImplementation::nextJumpFrame(std::vector<ReplayEvent> const& ev, int fromIndex,
                                         bool player2) {
    for (int index = fromIndex + 1; index < static_cast<int>(ev.size()); ++index) {
        if (ev[index].button != 1)
            continue;
        if (ev[index].player2 != player2)
            continue;
        if (ev[index].down)
            return ev[index].frame;
    }
    return -1;
}

int BakeMapImplementation::resolveRadius(int bakeRadius, BakeMethod method) {
    if (bakeRadius > 0)
        return bakeRadius;
    int base = StageConfig::ticksAll();
    if (method == BakeMethod::BruteForce) {
        return std::max(32, base * 4);
    }
    return std::max(8, base);
}

// After TPS remap, same-source-frame press+release can collapse to one bake
// frame. Push releases forward so every press is held ≥1 bake tick.
void BakeMapImplementation::ensureMinHoldTicks(std::vector<ReplayEvent>& ev, int minHold) {
    if (minHold <= 0 || ev.empty())
        return;
    for (size_t index = 0; index < ev.size(); ++index) {
        if (!ev[index].down || ev[index].button != 1)
            continue;
        for (size_t otherIndex = index + 1; otherIndex < ev.size(); ++otherIndex) {
            if (ev[otherIndex].button != ev[index].button)
                continue;
            if (ev[otherIndex].player2 != ev[index].player2)
                continue;
            if (ev[otherIndex].down)
                break; // next press
            if (ev[otherIndex].frame <= ev[index].frame) {
                ev[otherIndex].frame = ev[index].frame + minHold;
            }
            break;
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](auto const& a, auto const& b) {
        if (a.frame != b.frame)
            return a.frame < b.frame;
        // Downs before ups on the same frame (matches bake apply order).
        return a.down && !b.down;
    });
}

// Physics-bypass macros (MegaHack 1000–3000): keep author quantum 1:1.
// Mild FPS (60/360) still snap to live ~240 — discrete collisions match the game.
double BakeMapImplementation::resolveBakeTps(double srcTps, double liveTps) {
    if (srcTps > AssistClock::kPlanSnapTpsHi) {
        return std::min(srcTps, 4000.0);
    }
    if (liveTps < AssistClock::kPlanSnapTpsLo || liveTps > AssistClock::kPlanSnapTpsHi) {
        return AssistClock::kNominalTps;
    }
    return liveTps;
}

// Scale a ~240-tick radius/horizon to author TPS wall-time.
int BakeMapImplementation::scaleBakeTicks(int ticks, double bakeTps) {
    if (ticks <= 0)
        return ticks;
    if (bakeTps <= AssistClock::kPlanSnapTpsHi)
        return ticks;
    long long scaled =
        std::llround(static_cast<double>(ticks) * (bakeTps / AssistClock::kNominalTps));
    if (scaled < ticks)
        scaled = ticks;
    if (scaled > 2000)
        scaled = 2000;
    return static_cast<int>(scaled);
}

// Remap author FPS → bake TPS (no-op when bakeTps == srcTps), min-hold, P1-only.
std::vector<ReplayEvent>
BakeMapImplementation::prepareBakeTimeline(std::vector<ReplayEvent> const& src, double srcTps,
                                           double bakeTps, bool forceSoloP1) {
    std::vector<ReplayEvent> event = src;
    if (forceSoloP1) {
        // Keep real dual P2 only when P1 is majority; else MegaHack all-P2 → P1.
        int playerOne = 0;
        int playerTwo = 0;
        for (auto const& e : event) {
            if (e.player2)
                ++playerTwo;
            else
                ++playerOne;
        }
        if (playerTwo >= playerOne) {
            for (auto& e : event)
                e.player2 = false;
        } else {
            // Drop P2 dual clicks from P1 bake timeline (don't feed into ghost).
            event.erase(std::remove_if(event.begin(), event.end(),
                                       [](ReplayEvent const& e) { return e.player2; }),
                        event.end());
        }
    }
    if (srcTps > 0.0 && bakeTps > 0.0 && std::fabs(srcTps - bakeTps) >= 0.5) {
        float const srcDt = static_cast<float>(1.0 / srcTps);
        for (auto& e : event) {
            e.frame = AssistClock::get().remapTick(bakeTps, e.frame, srcDt);
        }
    }
    // MegaHack GDR is often 1-based (first physics tick = frame 1). Shift so the
    // opening tap lands on sim frame 0 — otherwise frame 0 falls into spawn spikes.
    {
        int minF = std::numeric_limits<int>::max();
        for (auto const& e : event) {
            if (e.frame < minF)
                minF = e.frame;
        }
        if (minF == 1) {
            for (auto& e : event)
                e.frame -= 1;
        }
    }
    BakeMapImplementation::ensureMinHoldTicks(event, 1);
    return event;
}

unsigned BakeMapImplementation::workerCount() {
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
    AssistLog::info(fmt::format("BAKE: loaded replay {} ({} events, {:.0f} tps, {})",
                                m_replay.sourceName, m_replay.events.size(), m_replay.tps,
                                m_replay.format));
}

void BakeMap::loadForLevel(GJGameLevel* level) {
    m_data = {};
    m_loaded = false;
    this->m_dirty = false;
    if (!level)
        return;
    m_data.levelKey = LevelProfile::makeKey(level);
    auto result = readFile();
    if (result.isOk() && !m_data.clicks.empty()) {
        m_loaded = true;
        AssistLog::debug(fmt::format("BAKE: loaded map {} ({} clicks, {}, {})", m_data.levelKey,
                                     m_data.clicks.size(), m_data.sourceReplay, m_data.method));
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
    if (!m_loaded || m_data.levelKey.empty())
        return;
    if (!this->m_dirty)
        return;
    auto result = writeFile();
    if (result.isOk()) {
        this->m_dirty = false;
        AssistLog::debug(fmt::format("BAKE: saved {} ({} clicks, {})", m_data.levelKey,
                                     m_data.clicks.size(), m_data.method));
    } else {
        AssistLog::debug(fmt::format("BAKE: save failed: {}", result.unwrapErr()));
    }
}

std::string BakeMap::statusLine() const {
    if (m_progress.running.load()) {
        return fmt::format("baking {}/{} ({:.0f}%)", m_progress.done.load(),
                           m_progress.total.load(), m_progress.fraction() * 100.f);
    }
    if (m_hasReplay && !m_data.clicks.empty()) {
        return fmt::format("{} | {} {} clicks", m_replay.sourceName, m_data.method,
                           m_data.clicks.size());
    }
    if (m_hasReplay)
        return fmt::format("{} | no bake", m_replay.sourceName);
    if (!m_data.clicks.empty()) {
        return fmt::format("{} {} clicks ({})", m_data.method, m_data.clicks.size(),
                           m_data.sourceReplay);
    }
    return "no bake";
}

std::string BakeMap::pauseStatusLine() const {
    std::string line = statusLine();
    if (m_hasReplay) {
        line = fmt::format("Replay: {} ({} ev @ {:.0f} tps)\n{}", m_replay.sourceName,
                           m_replay.events.size(), m_replay.tps, line);
    } else {
        line = fmt::format("Replay: (none) — Brute Bake needs none\n{}", line);
    }
    return line;
}

int BakeMap::refineSelfWindows(PlayLayer* layer, int startSnapshotSlot,
                               std::vector<BakedClick>& clicks, int bakeRadius, int budgetSims,
                               std::atomic<bool>* cancel) {
    if (!layer || startSnapshotSlot < 0 || clicks.empty() || budgetSims <= 0)
        return 0;
    if (isBaking())
        return 0;

    bakeRadius = BakeMapImplementation::resolveRadius(bakeRadius, BakeMethod::Replay);
    double tps = m_data.tps > 0.0 ? m_data.tps : 240.0;
    float deltaTime = static_cast<float>(1.0 / tps);
    int const horizon =
        ShadowSimulator::modelFor(layer->m_player1 ? layer->m_player1 : layer->m_player2,
                                  StageConfig::ticksAll())
            .cap;

    std::stable_sort(clicks.begin(), clicks.end(), [](auto const& a, auto const& b) {
        if (a.nominalFrame != b.nominalFrame)
            return a.nominalFrame < b.nominalFrame;
        return a.player2 < b.player2;
    });

    std::vector<ReplayEvent> timeline;
    timeline.reserve(clicks.size());
    for (auto const& c : clicks) {
        timeline.push_back(ReplayEvent{c.nominalFrame, 1, c.player2, c.down});
    }

    int simsLeft = budgetSims;
    int expanded = 0;
    auto cancelled = [&]() -> bool { return cancel && cancel->load(); };
    auto spend = [&]() -> bool {
        if (simsLeft <= 0)
            return true;
        --simsLeft;
        return false;
    };

    for (size_t index = 0; index < clicks.size(); ++index) {
        if (cancelled() || simsLeft <= 0)
            break;

        auto& click = clicks[index];
        int const F = click.nominalFrame;
        int next = -1;
        for (size_t otherIndex = index + 1; otherIndex < clicks.size(); ++otherIndex) {
            if (clicks[otherIndex].player2 != click.player2)
                continue;
            next = clicks[otherIndex].nominalFrame;
            break;
        }
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);

        // Keep timeline event frames in sync with current click nominals.
        timeline[index].frame = F;

        std::lock_guard lock(m_simMutex);
        if (spend())
            break;
        if (!testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(index), F, until,
                              deltaTime)) {
            // Nominal dies from start snapshot — keep point window.
            continue;
        }

        int lowerBound = std::max(0, F - bakeRadius);
        int upperBound = F;
        while (lowerBound < upperBound) {
            if (cancelled() || simsLeft <= 0)
                break;
            int mid = lowerBound + (upperBound - lowerBound) / 2;
            if (spend())
                break;
            if (testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(index), mid,
                                 until, deltaTime)) {
                upperBound = mid;
            } else {
                lowerBound = mid + 1;
            }
        }
        int earliest = lowerBound;

        lowerBound = F;
        upperBound = F + bakeRadius;
        while (lowerBound < upperBound) {
            if (cancelled() || simsLeft <= 0)
                break;
            int mid = lowerBound + (upperBound - lowerBound + 1) / 2;
            if (spend())
                break;
            if (testShiftedClick(layer, startSnapshotSlot, timeline, static_cast<int>(index), mid,
                                 until, deltaTime)) {
                lowerBound = mid;
            } else {
                upperBound = mid - 1;
            }
        }
        int latest = lowerBound;

        if (earliest > latest)
            std::swap(earliest, latest);
        if (earliest < click.earliest || latest > click.latest || earliest != click.nominalFrame ||
            latest != click.nominalFrame) {
            if (earliest != click.earliest || latest != click.latest)
                ++expanded;
            click.earliest = std::min(click.earliest, earliest);
            click.latest = std::max(click.latest, latest);
        }
    }

    AssistLog::debug(fmt::format("self-bake refine: {}/{} expanded, sims left {}", expanded,
                                 clicks.size(), simsLeft));
    return expanded;
}

std::vector<BakedClick const*> BakeMap::nextClicksAfter(float playerX, bool down, bool player2,
                                                        std::optional<GameModeKind> preferMode,
                                                        int maxCount) const {
    std::vector<BakedClick const*> out;
    if (maxCount <= 0 || !m_loaded)
        return out;

    auto collect = [&](bool requireMode) {
        std::vector<BakedClick const*> hits;
        for (auto const& c : m_data.clicks) {
            if (c.down != down || c.player2 != player2)
                continue;
            if (c.x <= playerX + 1.f)
                continue;
            if (requireMode && preferMode && c.mode != *preferMode)
                continue;
            hits.push_back(&c);
        }
        std::sort(hits.begin(), hits.end(), [](auto const* firstValue, auto const* secondValue) {
            if (firstValue->x != secondValue->x)
                return firstValue->x < secondValue->x;
            return firstValue->nominalFrame < secondValue->nominalFrame;
        });
        return hits;
    };

    auto hits = preferMode ? collect(true) : collect(false);
    if (hits.empty() && preferMode)
        hits = collect(false);
    for (auto const* candidate : hits) {
        out.push_back(candidate);
        if (static_cast<int>(out.size()) >= maxCount)
            break;
    }
    return out;
}

int BakeMap::mergeSelfClicks(std::vector<BakedClick> const& incoming) {
    if (incoming.empty() || !m_loaded)
        return 0;
    if (isBaking())
        return 0;

    bool const wasEmpty = m_data.clicks.empty();
    int touched = 0;

    for (auto const& neu : incoming) {
        BakedClick* match = nullptr;
        for (auto& old : m_data.clicks) {
            if (old.down != neu.down || old.player2 != neu.player2)
                continue;
            if (std::fabs(old.x - neu.x) > MERGE_X_TOL)
                continue;
            if (std::abs(old.nominalFrame - neu.nominalFrame) > MERGE_FRAME_TOL)
                continue;
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
            if (match->earliest != prevE || match->latest != prevL ||
                match->holdAssist != prevHold) {
                ++touched;
            }
            continue;
        }
        if (m_data.clicks.size() >= kMaxMapClicks)
            continue;
        m_data.clicks.push_back(neu);
        ++touched;
    }

    if (touched <= 0)
        return 0;

    if (wasEmpty || m_data.method == "self" || m_data.method.empty()) {
        m_data.method = "self";
        if (m_data.sourceReplay.empty())
            m_data.sourceReplay = "self-bake";
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

BakedClick const* BakeMap::findNear(float playerX, int approxFrame, bool down, bool player2,
                                    std::optional<GameModeKind> preferMode) const {
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
            if (c.down != down || c.player2 != player2)
                continue;
            if (requireMode && preferMode && c.mode != *preferMode)
                continue;
            float const deltaX = std::fabs(c.x - playerX);
            if (deltaX > LOOKUP_X_TOL)
                continue;
            if (approxFrame >= 0) {
                if (c.latest + kFramePastSlop < approxFrame)
                    continue; // long past
                if (approxFrame + kFrameFutureSlop < c.earliest)
                    continue; // too early
            }
            float const halfW = windowHalfW(c.earliest, c.latest);
            bool const inBand = deltaX <= halfW;
            bool const inTime =
                approxFrame < 0 || (approxFrame >= c.earliest && approxFrame <= c.latest);
            // Score: in-time+in-band best, then in-time, then in-band, then dx.
            float score = deltaX;
            if (!inBand)
                score += LOOKUP_X_TOL;
            if (!inTime)
                score += LOOKUP_X_TOL * 2.f;
            if (score < bestScore) {
                bestScore = score;
                best = &c;
            }
        }
        return best;
    };

    if (preferMode) {
        if (auto const* hit = pick(true))
            return hit;
    }
    return pick(false);
}

bool BakeMap::testShiftedClick(PlayLayer* layer, int startSnapshotSlot,
                               std::vector<ReplayEvent> timeline, int eventIndex, int testFrame,
                               int untilFrame, float deltaTime, int substeps) const {
    if (eventIndex < 0 || eventIndex >= static_cast<int>(timeline.size()))
        return false;
    timeline[static_cast<size_t>(eventIndex)].frame = testFrame;
    std::stable_sort(timeline.begin(), timeline.end(),
                     [](auto const& a, auto const& b) { return a.frame < b.frame; });
    auto result = ShadowSimulator::get().runBakeTimeline(
        layer, startSnapshotSlot, timeline, deltaTime, untilFrame, -1, true, false, true, substeps);
    return !result.died;
}

geode::Result<int> BakeMap::bake(PlayLayer* layer, int startSnapshotSlot, int bakeRadius,
                                 BakeMethod method) {
    if (m_progress.running.exchange(true)) {
        return geode::Err("Bake already running");
    }
    m_progress.cancel = false;
    m_progress.done = 0;
    m_progress.total = 0;
    m_progress.setStatus("starting…");

    auto finish = [&](geode::Result<int> result) -> geode::Result<int> {
        m_progress.running = false;
        m_progress.setStatus(result.isOk() ? fmt::format("done: {} clicks", result.unwrap())
                                           : fmt::format("failed: {}", result.unwrapErr()));
        return result;
    };

    if (method == BakeMethod::BruteForce) {
        return finish(bakeBrute(layer, startSnapshotSlot, bakeRadius));
    }
    return finish(bakeReplay(layer, startSnapshotSlot, bakeRadius));
}

geode::Result<int> BakeMap::bakeReplay(PlayLayer* layer, int startSnapshotSlot, int bakeRadius) {
    if (!layer)
        return geode::Err("No PlayLayer");
    if (!m_hasReplay || m_replay.events.empty())
        return geode::Err("No replay loaded");
    if (startSnapshotSlot < 0)
        return geode::Err("No start snapshot (reset once, then bake)");

    bakeRadius = BakeMapImplementation::resolveRadius(bakeRadius, BakeMethod::Replay);

    // World must match start snapshot: bake right after reset, not mid-run.
    // Mid-level orb claims / toggles make the ghost die immediately at start.
    int const attemptTick = AssistController::get().attemptTick();
    if (attemptTick > 60) {
        AssistLog::info(fmt::format("BAKE warn: attemptTick={} — prefer reset→pause→bake (mid-run "
                                    "world pollutes orbs/toggles)",
                                    attemptTick));
    }
    // Commit 4: never validate while TW is still building / stale-empty.
    if (StageConfig::triggerBake()) {
        m_progress.setStatus("waiting Trigger World…");
        std::string twErr;
        if (!TriggerWorldBaker::get().waitUntilReady(layer, 45000, twErr)) {
            return geode::Err(twErr.empty() ? "Trigger World not ready for bake" : twErr);
        }
        auto& tw = TriggerWorldStore::get();
        AssistLog::info(fmt::format("BAKE: TW ready tracks={} ready={} softcap={} ({})",
                                    tw.trackCount(), tw.readyTrackCount(), tw.softcapHit() ? 1 : 0,
                                    TriggerWorldBaker::get().statusLine()));
        if (tw.softcapHit()) {
            AssistLog::info(fmt::format(
                "BAKE warn: Trigger World softcap ({} tracks) — movers may be incomplete",
                tw.trackCount()));
        }
    }

    // Mild FPS (60/360) → resample onto live ~240. Physics-bypass (1000–3000)
    // stays 1:1 at author TPS so the ghost matches the bot quantum exactly.
    double const srcTps = m_replay.tps > 0.0 ? m_replay.tps : AssistClock::kNominalTps;
    double const liveTps = AssistController::get().estimatedTps();
    double const tps = BakeMapImplementation::resolveBakeTps(srcTps, liveTps);
    float const deltaTime = static_cast<float>(1.0 / tps);
    bakeRadius = BakeMapImplementation::scaleBakeTicks(bakeRadius, tps);

    std::vector<ReplayEvent> event =
        BakeMapImplementation::prepareBakeTimeline(m_replay.events, srcTps, tps, true);
    AssistLog::info(fmt::format("BAKE replay: timeline {} events @ {:.0f}←{:.0f} tps radius={} "
                                "(validate 1:1 then expand windows)",
                                event.size(), tps, srcTps, bakeRadius));

    BakeMapData out;
    out.levelKey = m_data.levelKey.empty() && layer->m_level ? LevelProfile::makeKey(layer->m_level)
                                                             : m_data.levelKey;
    if (out.levelKey.empty())
        out.levelKey = "unknown";
    out.sourceReplay = m_replay.sourceName;
    out.tps = tps;
    out.method = "replay";

    bool const doRelease = StageConfig::assistRelease();

    std::vector<ReplayJumpJob> jobs;
    for (int index = 0; index < static_cast<int>(event.size()); ++index) {
        auto const& e = event[static_cast<size_t>(index)];
        if (e.button != 1)
            continue;
        if (!e.down && !doRelease)
            continue;
        int F = e.frame;
        int next = BakeMapImplementation::nextJumpFrame(event, index, e.player2);
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        if (horizon <= 0)
            horizon = std::max(16, StageConfig::ticksAll() * 4);
        horizon = BakeMapImplementation::scaleBakeTicks(horizon, tps);
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);
        jobs.push_back(ReplayJumpJob{index, F, e.down, e.player2, until});
    }
    if (jobs.empty()) {
        return geode::Err("Replay has no Jump events to bake (import a macro with clicks)");
    }

    int lastFrame = 0;
    for (auto const& e : event) {
        if (e.frame > lastFrame)
            lastFrame = e.frame;
    }
    // Progress: validate frames + per-click expand.
    int const validateWeight = std::max(1, lastFrame);
    m_progress.total = validateWeight + static_cast<int>(jobs.size());
    m_progress.done = 0;
    m_progress.setStatus(fmt::format("validate macro 1:1 · {}f @ {:.0f} tps", lastFrame, tps));
    AssistLog::info(
        fmt::format("BAKE replay: validate 0..{} then expand {} clicks, radius {} @ {:.0f} tps",
                    lastFrame, jobs.size(), bakeRadius, tps));

    // Commit 6 Parity Gate: Soft portals Include + freeze ε before validate
    // (portal Include must not retune noise).
    {
        std::string portalDetail;
        if (!Tg2BakeFixture::bakeOfflinePortalsIncludeOk(portalDetail)) {
            return geode::Err(
                fmt::format("Bake portal policy failed ({}) — Soft portals must Include",
                            portalDetail.empty() ? "?" : portalDetail));
        }
        std::string parityDetail;
        AssistContracts140::applyLockedEps();
        if (!Tg2BakeFixture::parityGateOk(parityDetail)) {
            return geode::Err(
                fmt::format("Bake parity gate failed ({}) — ε LOCK required before validate",
                            parityDetail.empty() ? "?" : parityDetail));
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
        startPosAttempt ? 1 : 0, startX, attemptTick, TriggerWorldStore::get().trackCount()));

    // ── Validate 1:1 from start snapshot. Death ≠ hard fail: keep the
    // surviving prefix (Hybrid often only needs a StartPos part, not the
    // whole macro). Only frame-0 (no prefix) still errors.
    ShadowSimulator::BakeRunResult validate;
    {
        std::lock_guard lock(m_simMutex);
        int lastLogged = -1;
        validate = ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, event, deltaTime, lastFrame, -1, true, false, true, 1,
            [&](int frame) -> bool {
                if (m_progress.cancel.load())
                    return false;
                if (frame - lastLogged >= 64 || frame == lastFrame) {
                    lastLogged = frame;
                    m_progress.done = frame;
                    m_progress.setStatus(fmt::format("validate {}/{}", frame, lastFrame));
                }
                return true;
            });
    }
    if (m_progress.cancel.load())
        return geode::Err("Bake cancelled");

    int partialCut = -1; // frames >= cut are dropped; -1 = full macro OK
    if (validate.died) {
        // P2 drop stats for err / log (same policy as prepareBakeTimeline).
        int droppedP2 = 0;
        {
            int playerOne = 0;
            int playerTwo = 0;
            for (auto const& e : m_replay.events) {
                if (e.player2)
                    ++playerTwo;
                else
                    ++playerOne;
            }
            if (playerTwo < playerOne)
                droppedP2 = playerTwo;
        }
        auto const top3 = DevDiagDump::formatUnknownTop3(validate.unknownHist);
        int const twTracks = TriggerWorldStore::get().trackCount();
        auto const residual = Tg2BakeFixture::formatResidual(
            twTracks, validate.deathFrame, validate.deathObjId, validate.deathObjType);
        AssistLog::info(
            fmt::format("BAKE: macro dies in sim @ frame {} deathX={:.1f} deathY={:.1f} "
                        "objId={} objType={} gameId={} twTracks={} droppedP2={} "
                        "unknownTop3=[{}] soft={} hard={} residual={} startPos={} startX={:.1f}",
                        validate.deathFrame, validate.deathX, validate.deathY, validate.deathObjId,
                        validate.deathObjType, validate.deathObjGameId, twTracks, droppedP2, top3,
                        validate.unknownSoftHits, validate.unknownHardHits,
                        residual.empty() ? "-" : residual, startPosAttempt ? 1 : 0, startX));
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(
                    fmt::format("BAKE: validate death — wrote diag {}", diagDir.string()));
            }
        }

        int const cut = validate.deathFrame;
        if (cut <= 0) {
            // No surviving prefix — cannot expand any click from this pose.
            std::string errorMessage =
                fmt::format("Macro dies in sim at frame {} (x={:.0f}) deathObjId={} "
                            "twTracks={} unknownTop3=[{}] droppedP2={} startPos={} startX={:.0f}",
                            validate.deathFrame, validate.deathX, validate.deathObjId, twTracks,
                            top3, droppedP2, startPosAttempt ? 1 : 0, startX);
            if (!residual.empty()) {
                errorMessage += " — ";
                errorMessage += residual;
            }
            if (startPosAttempt) {
                errorMessage += " — StartPos bake: reset on the StartPos, pause, bake; "
                                "macro frames must be from the same StartPos attempt; "
                                "TW empty often kills frame 0 (wait Trigger World).";
            } else {
                errorMessage += " — ghost path ≠ bot. Reset→pause→Bake.";
            }
            errorMessage += " Diag: /diag/bakefail_* parityGate=eps-locked";
            return geode::Err(errorMessage);
        }

        // Partial prefix: keep clicks strictly before death; Hybrid parts OK.
        partialCut = cut;
        int const before = static_cast<int>(jobs.size());
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                  [cut](ReplayJumpJob const& j) { return j.frame >= cut; }),
                   jobs.end());
        for (auto& j : jobs) {
            if (j.until > cut)
                j.until = cut;
        }
        AssistLog::info(
            fmt::format("BAKE: partial prefix — died @{} keeping {}/{} clicks before cut "
                        "(Hybrid parts; not whole-macro)",
                        cut, jobs.size(), before));
        if (jobs.empty()) {
            return geode::Err(fmt::format("Macro dies at frame {} before any bake click "
                                          "(x={:.0f}, startPos={}, startX={:.0f}, twTracks={}). "
                                          "Diag: /diag/bakefail_*",
                                          cut, validate.deathX, startPosAttempt ? 1 : 0, startX,
                                          twTracks));
        }
    }

    m_progress.done = validateWeight;
    m_progress.setStatus(
        fmt::format("expand windows · {} clicks · radius {}", jobs.size(), bakeRadius));
    if (partialCut >= 0) {
        AssistLog::info(fmt::format("BAKE: expanding {} click windows (partial cut @{})",
                                    jobs.size(), partialCut));
    } else {
        AssistLog::info(
            fmt::format("BAKE: validate OK through frame {} — expanding {} click windows",
                        lastFrame, jobs.size()));
    }

    // ── Phase 2: binary-search earliest/latest around each click ───────
    for (auto const& job : jobs) {
        if (m_progress.cancel.load())
            return geode::Err("Bake cancelled");

        std::lock_guard lock(m_simMutex);
        auto sample = ShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, event,
                                                             deltaTime, job.frame, job.frame);
        float x = sample.sampled ? sample.xAtSample : 0.f;
        auto mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

        // Nominal already survived full validate — still re-check shift window.
        if (!testShiftedClick(layer, startSnapshotSlot, event, job.eventIndex, job.frame, job.until,
                              deltaTime)) {
            AssistLog::debug(fmt::format("BAKE: skip click @{} (shift re-check died)", job.frame));
            m_progress.done.fetch_add(1);
            continue;
        }

        int lowerBound = std::max(0, job.frame - bakeRadius);
        int upperBound = job.frame;
        while (lowerBound < upperBound) {
            int mid = lowerBound + (upperBound - lowerBound) / 2;
            if (testShiftedClick(layer, startSnapshotSlot, event, job.eventIndex, mid, job.until,
                                 deltaTime)) {
                upperBound = mid;
            } else {
                lowerBound = mid + 1;
            }
        }
        int earliest = lowerBound;

        lowerBound = job.frame;
        upperBound = job.frame + bakeRadius;
        while (lowerBound < upperBound) {
            int mid = lowerBound + (upperBound - lowerBound + 1) / 2;
            if (testShiftedClick(layer, startSnapshotSlot, event, job.eventIndex, mid, job.until,
                                 deltaTime)) {
                lowerBound = mid;
            } else {
                upperBound = mid - 1;
            }
        }
        int latest = lowerBound;

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
        m_progress.setStatus(fmt::format("expand {}/{} · @{} → [{},{}]",
                                         static_cast<int>(out.clicks.size()), jobs.size(),
                                         job.frame, earliest, latest));
    }

    int const nClicks = static_cast<int>(out.clicks.size());
    AssistLog::info(fmt::format("BAKE done clicks={} jobs={} lastFrame={} tps={:.0f} source={} "
                                "partialCut={} startPos={} (replay)",
                                nClicks, jobs.size(), lastFrame, tps, out.sourceReplay, partialCut,
                                startPosAttempt ? 1 : 0));

    if (nClicks <= 0) {
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(
                    fmt::format("BAKE: 0 windows after validate — diag {}", diagDir.string()));
            }
        }
        return geode::Err("Replay bake produced 0 windows — expand failed. "
                          "Diag: /diag/bakefail_*");
    }

    if (partialCut >= 0) {
        // Surface partial in progress status (scheduler success alert stays click count).
        m_progress.setStatus(fmt::format("partial {} windows (cut @{})", nClicks, partialCut));
    }

    m_data = std::move(out);
    m_loaded = true;
    this->m_dirty = true;
    save();
    return geode::Ok(nClicks);
}

geode::Result<int> BakeMap::bakeBrute(PlayLayer* layer, int startSnapshotSlot, int bakeRadius) {
    // Pure brute: no replay. Drive forward from start; at each death, exhaustively
    // try press/release timings in [death-radius, death] and keep windows that
    // don't die (progress further). Parallel job queue; GD physics mutexed.
    if (!layer)
        return geode::Err("No PlayLayer");
    if (startSnapshotSlot < 0)
        return geode::Err("No start snapshot (reset once, then Brute Bake)");
    if (!layer->m_player1)
        return geode::Err("No player");

    bakeRadius = BakeMapImplementation::resolveRadius(bakeRadius, BakeMethod::BruteForce);
    // Same physics quantum as live plan (AssistClock / estimatedTps = 1/simDt).
    double tps = AssistController::get().estimatedTps();
    if (tps < 60.0 || tps > 480.0)
        tps = 240.0;
    float const deltaTime = static_cast<float>(1.0 / tps);
    int const maxFrames = static_cast<int>(tps * 180.0); // 3 min physics cap
    int const maxClicks = 400;
    int const horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
    bool const doRelease = StageConfig::assistRelease();
    auto const model = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll());

    BakeMapData out;
    out.levelKey = m_data.levelKey.empty() && layer->m_level ? LevelProfile::makeKey(layer->m_level)
                                                             : m_data.levelKey;
    if (out.levelKey.empty())
        out.levelKey = "unknown";
    out.sourceReplay = "(brute)";
    out.tps = tps;
    out.method = "brute";

    m_progress.total = maxClicks;
    m_progress.done = 0;
    m_progress.setStatus(
        fmt::format("brute (no replay) · radius {} · serial @ {:.0f} tps", bakeRadius, tps));
    AssistLog::info(fmt::format(
        "BAKE brute: death-driven search, radius {}, horizon {}, serial main-thread @ {:.0f} tps",
        bakeRadius, horizon, tps));

    auto holdingAtEnd = [](std::vector<ReplayEvent> const& tl) {
        bool held = false;
        for (auto const& e : tl) {
            if (e.button == 1)
                held = e.down;
        }
        return held;
    };

    auto runUntil = [&](std::vector<ReplayEvent> const& tl, int until,
                        int sampleFrame) -> ShadowSimulator::BakeRunResult {
        std::lock_guard lock(m_simMutex);
        return ShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, tl, deltaTime,
                                                      until, sampleFrame);
    };

    class Trial {
    public:
        int actionFrame = 0;
        bool down = true;
        int releaseFrame = -1; // optional follow-up release
        int surviveUntil = -1; // death frame, or until if survived
        bool survived = false;
        float x = 0.f;
        GameModeKind mode = GameModeKind::Cube;

    private:
    };

    std::vector<ReplayEvent> committed;
    int lastDeath = -1;
    int stuck = 0;

    for (int step = 0; step < maxClicks; ++step) {
        if (m_progress.cancel.load())
            return geode::Err("Bake cancelled");

        auto baseline = runUntil(committed, maxFrames, -1);
        if (!baseline.died) {
            m_progress.setStatus(fmt::format("brute done — survived {}f with {} clicks", maxFrames,
                                             out.clicks.size()));
            AssistLog::info(fmt::format("BAKE brute: cleared horizon ({} frames) with {} clicks",
                                        maxFrames, out.clicks.size()));
            break;
        }

        int const D = baseline.deathFrame;
        if (D < 0)
            break;
        if (D == lastDeath) {
            if (++stuck >= 2) {
                AssistLog::info(
                    fmt::format("BAKE brute: stuck at death frame {} — stopping ({} clicks so far)",
                                D, out.clicks.size()));
                break;
            }
        } else {
            stuck = 0;
            lastDeath = D;
        }

        bool const held = holdingAtEnd(committed);
        int const lowerBound = std::max(0, D - bakeRadius);
        int const upperBound = D;
        int const untilProbe = std::min(maxFrames, D + std::max(horizon, 16));

        // Build candidate action list: (frame, down, optional release)
        class ActionSpec {
        public:
            int frame;
            bool down;
            int releaseFrame;

        private:
        };
        std::vector<ActionSpec> specs;
        specs.reserve(static_cast<size_t>((upperBound - lowerBound + 1) * 4));

        for (int tick = lowerBound; tick <= upperBound; ++tick) {
            if (!held) {
                // Press (hold)
                specs.push_back({tick, true, -1});
                // Tap variants for cube-like modes
                if (model.tapRelease || !model.continuous) {
                    for (int rel : {1, 2, 3, 4, 6, 8}) {
                        specs.push_back({tick, true, tick + rel});
                    }
                }
            }
            if (held || doRelease) {
                // Release if currently holding (or always probe release for assist-release)
                if (held) {
                    specs.push_back({tick, false, -1});
                }
            }
        }

        if (specs.empty()) {
            AssistLog::info(fmt::format("BAKE brute: no actions to try at death {}", D));
            break;
        }

        m_progress.setStatus(fmt::format("brute death @{} · probing {} combos · clicks {}", D,
                                         specs.size(), out.clicks.size()));

        std::vector<Trial> trials(specs.size());

        auto processSpec = [&](size_t index) {
            if (m_progress.cancel.load())
                return;
            auto const& sp = specs[index];
            auto tl = committed;
            tl.push_back(ReplayEvent{sp.frame, 1, false, sp.down});
            if (sp.releaseFrame >= 0) {
                tl.push_back(ReplayEvent{sp.releaseFrame, 1, false, false});
            }
            std::stable_sort(tl.begin(), tl.end(),
                             [](auto const& a, auto const& b) { return a.frame < b.frame; });

            Trial tr;
            tr.actionFrame = sp.frame;
            tr.down = sp.down;
            tr.releaseFrame = sp.releaseFrame;

            {
                std::lock_guard lock(m_simMutex);
                if (m_progress.cancel.load())
                    return;
                auto sample = ShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, tl,
                                                                     deltaTime, sp.frame, sp.frame);
                tr.x = sample.sampled ? sample.xAtSample : 0.f;
                tr.mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

                auto result = ShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, tl,
                                                                     deltaTime, untilProbe, -1);
                if (result.died) {
                    tr.surviveUntil = result.deathFrame;
                    tr.survived = result.deathFrame > D;
                } else {
                    tr.surviveUntil = untilProbe;
                    tr.survived = true;
                }
            }
            trials[index] = tr;
            m_progress.done.fetch_add(1);
        };

        // Inflate total roughly so the % bar moves during probes
        m_progress.total =
            std::max(m_progress.total.load(),
                     m_progress.done.load() + static_cast<int>(specs.size()) + (maxClicks - step));

        // Serial main-thread only (Cocos/PlayLayer are not thread-safe).
        for (size_t index = 0; index < specs.size(); ++index) {
            if (m_progress.cancel.load())
                return geode::Err("Bake cancelled");
            processSpec(index);
        }
        if (m_progress.cancel.load())
            return geode::Err("Bake cancelled");

        // Best survival past current death
        int bestUntil = D;
        for (auto const& tr : trials) {
            if (tr.surviveUntil > bestUntil)
                bestUntil = tr.surviveUntil;
        }
        if (bestUntil <= D) {
            AssistLog::info(
                fmt::format("BAKE brute: no combo survives past death {} — stopping", D));
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
        if (winners.empty())
            break;

        // Split by down vs release — prefer whichever group has more / better
        Trial const* pick = winners.front();
        int earliest = pick->actionFrame;
        int latest = pick->actionFrame;
        bool pickDown = pick->down;
        int pickRel = pick->releaseFrame;

        // Prefer down actions if any winner is a down
        bool anyDown = false;
        for (auto* width : winners) {
            if (width->down) {
                anyDown = true;
                break;
            }
        }
        earliest = std::numeric_limits<int>::max();
        latest = -1;
        for (auto* width : winners) {
            if (anyDown && !width->down)
                continue;
            if (width->actionFrame < earliest) {
                earliest = width->actionFrame;
                pick = width;
                pickDown = width->down;
                pickRel = width->releaseFrame;
            }
            if (width->actionFrame > latest)
                latest = width->actionFrame;
        }
        if (latest < earliest)
            break;

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
                holdTl.push_back(ReplayEvent{nominal, 1, false, true});
                auto dashProbe = runUntil(holdTl, pickRel, pickRel);
                rel.holdAssist = dashProbe.sampled && dashProbe.dashingAtSample;
            }
            out.clicks.push_back(rel);
        }

        committed.push_back(ReplayEvent{nominal, 1, false, pickDown});
        if (pickRel >= 0) {
            committed.push_back(ReplayEvent{pickRel, 1, false, false});
        }
        std::stable_sort(committed.begin(), committed.end(),
                         [](auto const& a, auto const& b) { return a.frame < b.frame; });

        AssistLog::debug(fmt::format("BAKE brute: {} @{} window [{},{}] survives→{} (was death {})",
                                     pickDown ? "press" : "release", nominal, earliest, latest,
                                     bestUntil, D));
        m_progress.setStatus(fmt::format("brute click #{} · {} @{} [{},{}] · next death hunt",
                                         out.clicks.size(), pickDown ? "press" : "release", nominal,
                                         earliest, latest));
    }

    int const nClicks = static_cast<int>(out.clicks.size());
    AssistLog::info(fmt::format("BAKE done clicks={} frames_cap={} tps={:.0f} (brute)", nClicks,
                                maxFrames, tps));

    if (nClicks <= 0) {
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (DevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                AssistLog::info(
                    fmt::format("BAKE brute: 0 windows — wrote diag bundle {}", diagDir.string()));
            }
        }
        return geode::Err("Brute found no safe clicks (died immediately / unsolvable from start). "
                          "Reset once, then Brute Bake. Diag under save dir /diag/bakefail_*.");
    }

    m_data = std::move(out);
    m_loaded = true;
    this->m_dirty = true;
    save();
    return geode::Ok(nClicks);
}

geode::Result<> BakeMap::writeFile() const {
    (void)geode::utils::file::createDirectoryAll(BakeMapImplementation::bakesDir());
    matjson::Value root = matjson::Value::object();
    root["version"] = 1;
    root["key"] = m_data.levelKey;
    root["source"] = m_data.sourceReplay;
    root["tps"] = m_data.tps;
    root["method"] = m_data.method;

    std::vector<matjson::Value> arr;
    arr.reserve(m_data.clicks.size());
    for (auto const& c : m_data.clicks) {
        matjson::Value otherIndex = matjson::Value::object();
        otherIndex["nominal"] = c.nominalFrame;
        otherIndex["earliest"] = c.earliest;
        otherIndex["latest"] = c.latest;
        otherIndex["down"] = c.down;
        otherIndex["player2"] = c.player2;
        otherIndex["x"] = c.x;
        otherIndex["mode"] = StageConfig::kindName(c.mode);
        if (c.holdAssist)
            otherIndex["holdAssist"] = true;
        arr.push_back(std::move(otherIndex));
    }
    root["clicks"] = arr;
    return geode::utils::file::writeToJson(BakeMapImplementation::bakePath(m_data.levelKey), root);
}

geode::Result<> BakeMap::readFile() {
    auto result = geode::utils::file::readJson(BakeMapImplementation::bakePath(m_data.levelKey));
    if (result.isErr())
        return geode::Err(result.unwrapErr());
    auto root = result.unwrap();

    if (root.contains("source"))
        m_data.sourceReplay = root["source"].asString().unwrapOr("");
    if (root.contains("tps"))
        m_data.tps = root["tps"].asDouble().unwrapOr(240.0);
    if (root.contains("method"))
        m_data.method = root["method"].asString().unwrapOr("replay");
    m_data.clicks.clear();
    if (!root.contains("clicks") || !root["clicks"].isArray())
        return geode::Ok();

    for (auto const& j : root["clicks"]) {
        BakedClick c;
        c.nominalFrame = static_cast<int>(j["nominal"].asInt().unwrapOr(0));
        c.earliest = static_cast<int>(j["earliest"].asInt().unwrapOr(c.nominalFrame));
        c.latest = static_cast<int>(j["latest"].asInt().unwrapOr(c.nominalFrame));
        c.down = j["down"].asBool().unwrapOr(true);
        c.player2 = j["player2"].asBool().unwrapOr(false);
        c.x = static_cast<float>(j["x"].asDouble().unwrapOr(0.0));
        c.holdAssist = j.contains("holdAssist") ? j["holdAssist"].asBool().unwrapOr(false) : false;
        auto modeName = j.contains("mode") ? j["mode"].asString().unwrapOr("cube") : "cube";
        c.mode = GameModeKind::Cube;
        for (int index = 0; index < StageConfig::MODE_COUNT; ++index) {
            if (modeName == StageConfig::kindName(static_cast<GameModeKind>(index))) {
                c.mode = static_cast<GameModeKind>(index);
                break;
            }
        }
        m_data.clicks.push_back(c);
    }
    return geode::Ok();
}
