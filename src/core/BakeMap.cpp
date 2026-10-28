#include "BakeMap.hpp"
#include "AssistController.hpp"
#include "ShadowSimulator.hpp"
#include "LevelProfile.hpp"
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
    m_loaded = true;
    auto res = readFile();
    if (res.isOk()) {
        AssistLog::debug(fmt::format(
            "BAKE: loaded map {} ({} clicks, {}, {})",
            m_data.levelKey, m_data.clicks.size(), m_data.sourceReplay, m_data.method
        ));
    }
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
            match->earliest = std::min(match->earliest, neu.earliest);
            match->latest = std::max(match->latest, neu.latest);
            if (match->earliest != prevE || match->latest != prevL) ++touched;
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

BakedClick const* BakeMap::findNear(
    float playerX, int approxFrame, bool down, bool player2,
    std::optional<GameModeKind> preferMode
) const {
    auto pick = [&](bool requireMode) -> BakedClick const* {
        BakedClick const* best = nullptr;
        float bestDx = LOOKUP_X_TOL + 1.f;
        for (auto const& c : m_data.clicks) {
            if (c.down != down || c.player2 != player2) continue;
            if (requireMode && preferMode && c.mode != *preferMode) continue;
            float dx = std::fabs(c.x - playerX);
            if (dx > LOOKUP_X_TOL) continue;
            if (approxFrame >= 0 && c.nominalFrame + 240 < approxFrame) continue;
            if (dx < bestDx) {
                bestDx = dx;
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
    float dt
) const {
    if (eventIndex < 0 || eventIndex >= static_cast<int>(timeline.size())) return false;
    timeline[static_cast<size_t>(eventIndex)].frame = testFrame;
    std::stable_sort(timeline.begin(), timeline.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    auto r = ShadowSimulator::get().runBakeTimeline(
        layer, startSnapshotSlot, timeline, dt, untilFrame, -1
    );
    return !r.died;
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
    double tps = m_replay.tps > 0.0 ? m_replay.tps : 240.0;
    float dt = static_cast<float>(1.0 / tps);

    BakeMapData out;
    out.levelKey = m_data.levelKey.empty() && layer->m_level
        ? LevelProfile::makeKey(layer->m_level)
        : m_data.levelKey;
    if (out.levelKey.empty()) out.levelKey = "unknown";
    out.sourceReplay = m_replay.sourceName;
    out.tps = tps;
    out.method = "replay";

    auto const& ev = m_replay.events;
    bool const doRelease = StageConfig::assistRelease();

    std::vector<JumpJob> jobs;
    for (int i = 0; i < static_cast<int>(ev.size()); ++i) {
        auto const& e = ev[static_cast<size_t>(i)];
        if (e.button != 1) continue;
        if (!e.down && !doRelease) continue;
        int F = e.frame;
        int next = nextJumpFrame(ev, i, e.player2);
        int horizon = ShadowSimulator::modelFor(layer->m_player1, StageConfig::ticksAll()).cap;
        int until = next > 0 ? std::min(next, F + horizon) : F + horizon;
        until = std::max(until, F + 1);
        jobs.push_back(JumpJob{ i, F, e.down, e.player2, until });
    }

    m_progress.total = static_cast<int>(jobs.size());
    m_progress.setStatus(fmt::format("replay bake · {} clicks · radius {}", jobs.size(), bakeRadius));
    AssistLog::info(fmt::format(
        "BAKE replay: {} jobs, radius {}, 1 thread (binary search)",
        jobs.size(), bakeRadius
    ));

    for (auto const& job : jobs) {
        if (m_progress.cancel.load()) return Err("Bake cancelled");

        std::lock_guard lock(m_simMutex);
        auto sample = ShadowSimulator::get().runBakeTimeline(
            layer, startSnapshotSlot, ev, dt, job.frame, job.frame
        );
        float x = sample.sampled ? sample.xAtSample : 0.f;
        auto mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

        if (!testShiftedClick(layer, startSnapshotSlot, ev, job.eventIndex, job.frame, job.until, dt)) {
            AssistLog::debug(fmt::format("BAKE: skip click @{} (nominal dies)", job.frame));
            m_progress.done.fetch_add(1);
            continue;
        }

        int lo = std::max(0, job.frame - bakeRadius);
        int hi = job.frame;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (testShiftedClick(layer, startSnapshotSlot, ev, job.eventIndex, mid, job.until, dt)) {
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
            if (testShiftedClick(layer, startSnapshotSlot, ev, job.eventIndex, mid, job.until, dt)) {
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
        out.clicks.push_back(c);

        m_progress.done.fetch_add(1);
        m_progress.setStatus(fmt::format(
            "replay {}/{} · @{} → [{},{}]",
            m_progress.done.load(), m_progress.total.load(),
            job.frame, earliest, latest
        ));
    }

    m_data = std::move(out);
    m_loaded = true;
    this->m_dirty = true;
    save();
    AssistLog::info(fmt::format(
        "BAKE: {} clicks from {} @ {:.0f} tps (replay)",
        m_data.clicks.size(), m_data.sourceReplay, m_data.tps
    ));
    return Ok(static_cast<int>(m_data.clicks.size()));
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

    if (out.clicks.empty()) {
        return Err("Brute found no safe clicks (died immediately / unsolvable from start)");
    }

    m_data = std::move(out);
    m_loaded = true;
    this->m_dirty = true;
    save();
    AssistLog::info(fmt::format(
        "BAKE: {} clicks (brute, no replay) @ {:.0f} tps (serial)",
        m_data.clicks.size(), m_data.tps
    ));
    return Ok(static_cast<int>(m_data.clicks.size()));
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
