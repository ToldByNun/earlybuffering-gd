#include "bake_brute_generator.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../dev/reporting/dev_diag_dump.hpp"
#include "../../assist/assist_controller.hpp"
#include "../../learning/level_profile.hpp"
#include "../../replay/replay_import.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../bake_map.hpp"
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

geode::Result<int> EbBakeBruteGenerator::bakeBrute(EbBakeMap& owner, PlayLayer* layer,
                                                 int startSnapshotSlot, int bakeRadius) {
    // Pure brute: no replay. Drive forward from start; at each death, exhaustively
    // try press/release timings in [death-radius, death] and keep windows that
    // don't die (progress further). Parallel job queue; GD physics mutexed.
    if (!layer)
        return geode::Err("No PlayLayer");
    if (startSnapshotSlot < 0)
        return geode::Err("No start snapshot (reset once, then Brute Bake)");
    if (!layer->m_player1)
        return geode::Err("No player");

    bakeRadius = EbBakeMapImplementation::resolveRadius(bakeRadius, BakeMethod::BruteForce);
    // Same physics quantum as live plan (AssistClock / estimatedTps = 1/simDt).
    double tps = EbAssistController::get().estimatedTps();
    if (tps < 60.0 || tps > 480.0)
        tps = 240.0;
    float const deltaTime = static_cast<float>(1.0 / tps);
    int const maxFrames = static_cast<int>(tps * 180.0); // 3 min physics cap
    int const maxClicks = 400;
    int const horizon = EbShadowSimulator::modelFor(layer->m_player1, EbStageConfig::ticksAll()).cap;
    bool const doRelease = EbStageConfig::assistRelease();
    auto const model = EbShadowSimulator::modelFor(layer->m_player1, EbStageConfig::ticksAll());

    EbBakeMapData out;
    out.levelKey = owner.m_data.levelKey.empty() && layer->m_level
                       ? EbLevelProfile::makeKey(layer->m_level)
                       : owner.m_data.levelKey;
    if (out.levelKey.empty())
        out.levelKey = "unknown";
    out.sourceReplay = "(brute)";
    out.tps = tps;
    out.method = "brute";

    owner.m_progress.total = maxClicks;
    owner.m_progress.done = 0;
    owner.m_progress.setStatus(
        fmt::format("brute (no replay) · radius {} · serial @ {:.0f} tps", bakeRadius, tps));
    EbAssistLog::info(fmt::format(
        "BAKE brute: death-driven search, radius {}, horizon {}, serial main-thread @ {:.0f} tps",
        bakeRadius, horizon, tps));

    auto holdingAtEnd = [](std::vector<EbReplayEvent> const& tl) {
        bool held = false;
        for (auto const& e : tl) {
            if (e.button == 1)
                held = e.down;
        }
        return held;
    };

    auto runUntil = [&](std::vector<EbReplayEvent> const& tl, int until,
                        int sampleFrame) -> EbShadowSimulator::BakeRunResult {
        std::lock_guard lock(owner.m_simMutex);
        return EbShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, tl, deltaTime,
                                                      until, sampleFrame);
    };

    class EbTrial {
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

    std::vector<EbReplayEvent> committed;
    int lastDeath = -1;
    int stuck = 0;

    for (int step = 0; step < maxClicks; ++step) {
        if (owner.m_progress.cancel.load())
            return geode::Err("Bake cancelled");

        auto baseline = runUntil(committed, maxFrames, -1);
        if (!baseline.died) {
            owner.m_progress.setStatus(fmt::format("brute done — survived {}f with {} clicks",
                                                   maxFrames, out.clicks.size()));
            EbAssistLog::info(fmt::format("BAKE brute: cleared horizon ({} frames) with {} clicks",
                                        maxFrames, out.clicks.size()));
            break;
        }

        int const D = baseline.deathFrame;
        if (D < 0)
            break;
        if (D == lastDeath) {
            if (++stuck >= 2) {
                EbAssistLog::info(
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
        class EbActionSpec {
        public:
            int frame;
            bool down;
            int releaseFrame;

        private:
        };
        std::vector<EbActionSpec> specs;
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
            EbAssistLog::info(fmt::format("BAKE brute: no actions to try at death {}", D));
            break;
        }

        owner.m_progress.setStatus(fmt::format("brute death @{} · probing {} combos · clicks {}", D,
                                               specs.size(), out.clicks.size()));

        std::vector<EbTrial> trials(specs.size());

        auto processSpec = [&](size_t index) {
            if (owner.m_progress.cancel.load())
                return;
            auto const& sp = specs[index];
            auto tl = committed;
            tl.push_back(EbReplayEvent{sp.frame, 1, false, sp.down});
            if (sp.releaseFrame >= 0) {
                tl.push_back(EbReplayEvent{sp.releaseFrame, 1, false, false});
            }
            std::stable_sort(tl.begin(), tl.end(),
                             [](auto const& a, auto const& b) { return a.frame < b.frame; });

            EbTrial tr;
            tr.actionFrame = sp.frame;
            tr.down = sp.down;
            tr.releaseFrame = sp.releaseFrame;

            {
                std::lock_guard lock(owner.m_simMutex);
                if (owner.m_progress.cancel.load())
                    return;
                auto sample = EbShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, tl,
                                                                     deltaTime, sp.frame, sp.frame);
                tr.x = sample.sampled ? sample.xAtSample : 0.f;
                tr.mode = sample.sampled ? sample.modeAtSample : GameModeKind::Cube;

                auto result = EbShadowSimulator::get().runBakeTimeline(layer, startSnapshotSlot, tl,
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
            owner.m_progress.done.fetch_add(1);
        };

        // Inflate total roughly so the % bar moves during probes
        owner.m_progress.total = std::max(owner.m_progress.total.load(),
                                          owner.m_progress.done.load() +
                                              static_cast<int>(specs.size()) + (maxClicks - step));

        // Serial main-thread only (Cocos/PlayLayer are not thread-safe).
        for (size_t index = 0; index < specs.size(); ++index) {
            if (owner.m_progress.cancel.load())
                return geode::Err("Bake cancelled");
            processSpec(index);
        }
        if (owner.m_progress.cancel.load())
            return geode::Err("Bake cancelled");

        // Best survival past current death
        int bestUntil = D;
        for (auto const& tr : trials) {
            if (tr.surviveUntil > bestUntil)
                bestUntil = tr.surviveUntil;
        }
        if (bestUntil <= D) {
            EbAssistLog::info(
                fmt::format("BAKE brute: no combo survives past death {} — stopping", D));
            break;
        }

        // Prefer press-downs that reach bestUntil (within 2 frames slack)
        int const slack = 2;
        std::vector<EbTrial const*> winners;
        for (auto const& tr : trials) {
            if (tr.surviveUntil + slack >= bestUntil && tr.surviveUntil > D) {
                winners.push_back(&tr);
            }
        }
        if (winners.empty())
            break;

        // Split by down vs release — prefer whichever group has more / better
        EbTrial const* pick = winners.front();
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

        EbBakedClick c;
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
            EbBakedClick rel;
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
                holdTl.push_back(EbReplayEvent{nominal, 1, false, true});
                auto dashProbe = runUntil(holdTl, pickRel, pickRel);
                rel.holdAssist = dashProbe.sampled && dashProbe.dashingAtSample;
            }
            out.clicks.push_back(rel);
        }

        committed.push_back(EbReplayEvent{nominal, 1, false, pickDown});
        if (pickRel >= 0) {
            committed.push_back(EbReplayEvent{pickRel, 1, false, false});
        }
        std::stable_sort(committed.begin(), committed.end(),
                         [](auto const& a, auto const& b) { return a.frame < b.frame; });

        EbAssistLog::debug(fmt::format("BAKE brute: {} @{} window [{},{}] survives→{} (was death {})",
                                     pickDown ? "press" : "release", nominal, earliest, latest,
                                     bestUntil, D));
        owner.m_progress.setStatus(fmt::format("brute click #{} · {} @{} [{},{}] · next death hunt",
                                               out.clicks.size(), pickDown ? "press" : "release",
                                               nominal, earliest, latest));
    }

    int const nClicks = static_cast<int>(out.clicks.size());
    EbAssistLog::info(fmt::format("BAKE done clicks={} frames_cap={} tps={:.0f} (brute)", nClicks,
                                maxFrames, tps));

    if (nClicks <= 0) {
        {
            std::filesystem::path diagDir;
            std::string diagErr;
            if (EbDevDiagDump::dumpBakeFail(diagDir, diagErr)) {
                EbAssistLog::info(
                    fmt::format("BAKE brute: 0 windows — wrote diag bundle {}", diagDir.string()));
            }
        }
        return geode::Err("Brute found no safe clicks (died immediately / unsolvable from start). "
                          "Reset once, then Brute Bake. Diag under save dir /diag/bakefail_*.");
    }

    owner.m_data = std::move(out);
    owner.m_loaded = true;
    owner.m_dirty = true;
    owner.save();
    return geode::Ok(nClicks);
}
