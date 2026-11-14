#include "assist_bake_reuse.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../debug/assist_log.hpp"
#include "../../../../debug/lag_probe.hpp"
#include "../../../../debug/perf_trace.hpp"
#include "../../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../../dev/capture/dev_trace_store.hpp"
#include "../../../bake/bake_map.hpp"
#include "../../../bake/bake_verifier.hpp"
#include "../../../bake/hybrid_verify_cache.hpp"
#include "../../../diagnostics/assist_real_metrics.hpp"
#include "../../../sim/shadow_simulator.hpp"
#include "../../assist_clock.hpp"
#include "../../assist_controller.hpp"
#include "../../contracts/assist_verdict.hpp"
#include "../../contracts/live_plan_contracts.hpp"
#include "live_frame_budget_scope.hpp"
#include <algorithm>
#include <cmath>

bool EbAssistBakeReuse::tryBakeAssist(EbAssistController& controller, PlayLayer* playLayer,
                                      PlayerObject* player, bool player1, bool down, int effectTick,
                                      float firstDt, bool& handled, EbPressSimBudget* sharedBudget,
                                      bool* verifyAttempted, EbHybridPressReuse* reuse) {
    handled = false;
    if (verifyAttempted)
        *verifyAttempted = false;
    if (reuse)
        *reuse = {};
    auto mode = EbStageConfig::assistMode();
    if (mode == AssistMode::Live)
        return true;

    bool const bakeOnly = mode == AssistMode::Replay || mode == AssistMode::BruteForce;
    bool const hybrid = mode == AssistMode::Hybrid;
    float const playerX = player->getPositionX();

    // Loud miss line for Hybrid/Replay when a bake window was in play but we
    // did not arm (screenshot bug: green box, press, nothing).
    auto logMissBakeWin = [&](char const* branch, EbBakedClick const* width, int earliestTick,
                              int latestTick, int delay, int approxFrame) {
        float const clickX = width ? width->x : -1.f;
        float const deltaX = width ? std::fabs(clickX - playerX) : -1.f;
        EbAssistLog::info(
            fmt::format("MISS-BAKE-WIN {} {} pX={:.1f} clickX={:.1f} dx={:.1f} "
                        "approxF={} nom={} eF={} lF={} effectT={} eT={} lT={} delay={} "
                        "mode={} hybrid={}",
                        branch, down ? "press" : "release", playerX, clickX, deltaX, approxFrame,
                        width ? width->nominalFrame : -1, width ? width->earliest : -1,
                        width ? width->latest : -1, effectTick, earliestTick, latestTick, delay,
                        EbStageConfig::kindName(EbStageConfig::kindOf(player)), hybrid ? 1 : 0));
        controller.m_lastBakeHint = fmt::format("MISS {}", branch);
    };

    auto fallThroughOrPass = [&](char const* reason, AssistReason bakeReason) -> bool {
        EbAssistLog::debug(fmt::format("bake: {} → {}", reason, hybrid ? "live" : "pass"));
        if (hybrid) {
            handled = false;
            // Keep MISS hint if already set; else stamp fallthrough.
            if (controller.m_lastBakeHint.rfind("MISS", 0) != 0) {
                controller.m_lastBakeHint = fmt::format("{} → live", reason);
            } else {
                controller.m_lastBakeHint = fmt::format("{} → live", controller.m_lastBakeHint);
            }
            EbAssistLog::debug(fmt::format(
                "bake hybrid: {} — fall through to live (foresight/miss-assist)", reason));
            controller.updateOverlay();
            return true;
        }
        handled = true;
        if (controller.m_lastBakeHint.rfind("MISS", 0) != 0) {
            controller.m_lastBakeHint = reason;
        }
        EbDevCycleRecorder::get().noteGatePass(bakeReason, reason);
        controller.noteVerdict(bakeReason, reason);
        return true;
    };

    auto const& bake = EbBakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) {
        if (bakeOnly) {
            handled = true;
            controller.m_lastBakeHint =
                mode == AssistMode::BruteForce ? "brute missing" : "bake missing";
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassBakeMissing, "bake-missing");
            controller.noteVerdict(AssistReason::PassBakeMissing, "bake-missing");
            return true;
        }
        return fallThroughOrPass("bake missing", AssistReason::PassBakeMissing);
    }

    double tps = bake.data().tps > 0.0 ? bake.data().tps : EbAssistClock::kNominalTps;
    int approxFrame =
        EbAssistClock::get().remapTick(tps, controller.m_attemptTick, controller.m_simDt);
    auto const kind = EbStageConfig::kindOf(player);
    auto const* win = bake.findNear(playerX, approxFrame, down, !player1, kind);
    if (!win) {
        // Only loud-miss when a drawn/nearby window exists but findNear skipped it
        // (mode/frame filter) — not every press far from bake.
        EbBakedClick const* spatialNear = nullptr;
        float bestDx = EbBakeMap::LOOKUP_X_TOL + 1.f;
        for (auto const& c : bake.data().clicks) {
            if (c.down != down || c.player2 != (!player1))
                continue;
            float const deltaX = std::fabs(c.x - playerX);
            float const halfW = EbBakeMap::windowHalfW(c.earliest, c.latest);
            if (deltaX > EbBakeMap::LOOKUP_X_TOL && deltaX > halfW)
                continue;
            if (deltaX < bestDx) {
                bestDx = deltaX;
                spatialNear = &c;
            }
        }
        if (spatialNear) {
            logMissBakeWin("no-window", spatialNear, -1, -1, -1, approxFrame);
        }
        if (bakeOnly) {
            handled = true;
            controller.m_lastBakeHint = spatialNear ? "MISS no-window" : "no window";
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassBakeNoWindow,
                                                   "bake-no-window");
            controller.noteVerdict(AssistReason::PassBakeNoWindow, "bake-no-window");
            return true;
        }
        return fallThroughOrPass("no window", AssistReason::PassBakeNoWindow);
    }

    // Copy window fields for Live fallthrough (no raw bake-vector pointers).
    if (reuse) {
        reuse->hasWindow = true;
        reuse->earliest = win->earliest;
        reuse->latest = win->latest;
        reuse->nominalFrame = win->nominalFrame;
        reuse->bakeTps = tps;
    }

    float const halfW = EbBakeMap::windowHalfW(win->earliest, win->latest);
    bool const spatialIn = std::fabs(playerX - win->x) <= halfW;

    // ABSOLUTE bake frame → live attempt tick (inverse of remapTick).
    // Old relative mapping (effectTick + frame - nominal) recentered the window
    // on the press: with LOOKUP_X_TOL≈120px (~100f early) that fired immediately
    // while still far before earliest — green box, press, death.
    float const simDt = controller.m_simDt > 0.f
                            ? controller.m_simDt
                            : static_cast<float>(1.0 / EbAssistClock::kNominalTps);
    auto& clock = EbAssistClock::get();
    int earliestTick = clock.unmapTick(tps, win->earliest, simDt);
    int latestTick = clock.unmapTick(tps, win->latest, simDt);
    int const nominalTick = clock.unmapTick(tps, win->nominalFrame, simDt);
    if (latestTick < earliestTick)
        std::swap(latestTick, earliestTick);

    int const limit = controller.activeLimit(player);

    // Past latest: only salvage a tiny tick slop while still in the drawn band.
    // Deeply late + still spatially overlapping a wide box used to arm and die.
    static constexpr int kPastLatestSalvageSlop = 2;
    bool salvagedPastLatest = false;
    if (effectTick > latestTick) {
        int const lateBy = effectTick - latestTick;
        if (spatialIn && lateBy <= kPastLatestSalvageSlop) {
            salvagedPastLatest = true;
            EbAssistLog::info(fmt::format(
                "MISS-BAKE-WIN past-latest→salvage-now {} pX={:.1f} clickX={:.1f} "
                "dx={:.1f} halfW={:.1f} effectT={} lT={} lateBy={} eF={} lF={}",
                down ? "press" : "release", playerX, win->x, std::fabs(playerX - win->x), halfW,
                effectTick, latestTick, lateBy, win->earliest, win->latest));
        } else {
            logMissBakeWin(lateBy > kPastLatestSalvageSlop ? "past-latest-deep" : "past-latest",
                           win, earliestTick, latestTick, -1, approxFrame);
            return fallThroughOrPass("past latest", AssistReason::PassBakePastLatest);
        }
    }

    // Absolute template delay:
    //  - before earliest → wait until earliest (never fire early of window)
    //  - inside window but before nominal → wait until nominal (replay center)
    //  - at/after nominal (still ≤ latest) or salvaged → fire now
    // If assist limit cannot reach earliest, do not clamp-fire early — miss instead.
    int delay = 0;
    if (!salvagedPastLatest) {
        if (effectTick < earliestTick) {
            delay = earliestTick - effectTick;
            if (limit > 0 && delay > limit) {
                logMissBakeWin("limit-before-earliest", win, earliestTick, latestTick, delay,
                               approxFrame);
                EbAssistLog::info(fmt::format("MISS-BAKE-WIN limit-before-earliest need={} lim={} "
                                              "effectT={} eT={} — refuse early fire",
                                              delay, limit, effectTick, earliestTick));
                return fallThroughOrPass("limit before earliest", AssistReason::PassBakeNoWindow);
            }
        } else if (effectTick < nominalTick) {
            // Full wait to nominal — do NOT clamp to ticks-all/mode limit.
            // Clamping fired mid-window (early) and made verify use the wrong
            // delay → Hybrid fallthrough → HUS even earlier. Buffer.limit is
            // raised below to cover this wait (template > live tick budget).
            delay = nominalTick - effectTick;
            if (effectTick + delay > latestTick) {
                delay = std::max(0, latestTick - effectTick);
            }
        }
        if (delay < 0)
            delay = 0;
    }

    auto runVerify = [&](int delay, EbBakeVerifier::Result& outVr) -> void {
        float const firstStepDelta =
            (firstDt > 0.f && firstDt <= controller.m_simDt) ? firstDt : controller.m_simDt;
        if (verifyAttempted)
            *verifyAttempted = true;

        EbHybridVerifyCache::Key cacheKey;
        cacheKey.nominalFrame = win->nominalFrame;
        cacheKey.earliest = win->earliest;
        cacheKey.latest = win->latest;
        cacheKey.delayTicks = delay;
        cacheKey.limit = limit;
        cacheKey.down = down;
        cacheKey.player2 = !player1;
        cacheKey.mode = kind;

        bool cacheHit = false;
        if (hybrid) {
            cacheHit = EbHybridVerifyCache::get().tryGet(cacheKey, playerX, outVr);
        }
        if (cacheHit) {
            EbLagProbe::Scope const hyVerify(
                hybrid ? "eb.hybrid.verify" : nullptr,
                fmt::format("d={} lim={} cache=1 ok={}", delay, limit, outVr.ok ? 1 : 0));
            (void)hyVerify;
            return;
        }
        EbLagProbe::Scope const hyVerify(hybrid ? "eb.hybrid.verify" : nullptr,
                                         fmt::format("d={} lim={} cache=0", delay, limit));
        if (sharedBudget) {
            int const limitUs =
                EbLivePlanContracts::intendedLiveBudgetUs(EbStageConfig::simBudgetUs());
            int const remainingUs = controller.m_framePlanBudget.remaining(
                limitUs, EbLiveFrameBudgetScope::nowUs(), EbLiveFrameBudgetScope::kExitReserveUs);
            sharedBudget->usLeft = std::min(sharedBudget->usLeft, remainingUs);
            if (sharedBudget->usLeft <= 0) {
                outVr = {};
                outVr.budgetHit = true;
                return;
            }
        }
        outVr = EbBakeVerifier::verify(playLayer, player, down, delay, limit, controller.m_simDt,
                                       firstStepDelta, sharedBudget, hybrid);
        if (hybrid) {
            EbHybridVerifyCache::get().put(cacheKey, playerX, outVr);
        }
        (void)hyVerify;
    };

    // Live re-verify. Never soft-trust budgetHit.
    // d0 salvage only at/after nominal — before nominal it would fire early
    // (Hybrid then Live/HUS made that worse: pass-hopeless → mid-window inject).
    // Still fail before nominal → Pass (no Live). At/after nominal → Hybrid Live.
    EbBakeVerifier::Result vr{};
    vr.ok = true;
    if (limit > 0 && controller.m_simDt > 0.f) {
        runVerify(delay, vr);
        if (reuse)
            reuse->verifyThreat = vr.threatZone;

        bool const beforeNominal = effectTick < nominalTick;
        if (!vr.ok && delay > 0 && !beforeNominal) {
            EbBakeVerifier::Result vr0{};
            runVerify(0, vr0);
            if (vr0.ok) {
                EbAssistLog::info(fmt::format("MISS-BAKE-WIN verify-fail→salvage-d0 {} delayWas={} "
                                              "pX={:.1f} clickX={:.1f} eF={} lF={}",
                                              down ? "press" : "release", delay, playerX, win->x,
                                              win->earliest, win->latest));
                delay = 0;
                vr = vr0;
                if (reuse)
                    reuse->verifyThreat = vr.threatZone;
            }
        }

        if (!vr.ok) {
            char const* why = "verify-fail";
            if (vr.budgetHit)
                why = "verify-budget";
            else if (vr.diedBeforeAction)
                why = "verify-died-before";
            else if (!vr.clearsThreat)
                why = "verify-threat";
            logMissBakeWin(why, win, earliestTick, latestTick, delay, approxFrame);
            if (hybrid && beforeNominal) {
                // Window known, still before replay center — Live must not
                // invent an earlier fire via PassHopeless→HUS (session early assist).
                handled = true;
                controller.m_lastBakeHint = fmt::format("MISS {} → pass (before-nom)", why);
                EbAssistLog::info(
                    fmt::format("MISS-BAKE-WIN {} before-nominal → Pass (no Live/HUS) "
                                "effectT={} nT={} eF={} lF={}",
                                why, effectTick, nominalTick, win->earliest, win->latest));
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassBakeVerifyFail,
                                                       "verify-before-nominal");
                controller.noteVerdict(AssistReason::PassBakeVerifyFail, "verify-before-nominal");
                controller.updateOverlay();
                return true;
            }
            return fallThroughOrPass(why, AssistReason::PassBakeVerifyFail);
        }
    }

    // Template arm — press and release share this path (purple release gated
    // upstream by shouldPlanRelease). No silent PassBakeInWindow.
    char const* armTag = "now";
    if (salvagedPastLatest)
        armTag = "salvage";
    else if (delay > 0 && effectTick < earliestTick)
        armTag = "wait-earliest";
    else if (delay > 0)
        armTag = "wait-nominal";
    char const* armWhere = salvagedPastLatest ? "bake-salvage-now"
                           : (delay > 0 && effectTick < earliestTick)
                               ? "bake-wait-earliest"
                               : (delay > 0 ? "bake-wait-nominal" : "bake-now");

    handled = true;
    // Overlay ownership: bake↓/↑ tag window + why (salvage / wait-nom / now).
    controller.m_lastBakeHint = fmt::format("bake{} {} [{},{}] +{}", down ? "↓" : "↑", armTag,
                                            win->earliest, win->latest, delay);

    controller.m_buffer = {};
    controller.m_buffer.state = BufferState::Buffered;
    controller.m_buffer.down = down;
    controller.m_buffer.player1 = player1;
    controller.m_buffer.pressTick = effectTick;
    controller.m_buffer.planDelay = delay;
    controller.m_buffer.limit = std::max(limit, std::max(1, delay));
    controller.m_buffer.fireTick = controller.resolveFireTick(effectTick, delay, delay, false);
    controller.m_buffer.expectedJumpTick = controller.m_buffer.fireTick;
    controller.m_buffer.decisionX = playerX;
    controller.m_buffer.mode = kind;
    controller.m_buffer.fromBake = true;

    // Orb heuristic only for presses — releases keep pure template timing.
    if (down && EbStageConfig::assistOrbs()) {
        int const dMin = std::max(0, earliestTick - effectTick);
        int const dMax = std::max(dMin, latestTick - effectTick);
        bool const nearOrb = EbShadowSimulator::spatialOrbHint(
            playLayer, player, std::max(limit, dMax + 2), controller.m_simDt);
        if (nearOrb) {
            controller.m_buffer.requireOrb = true;
            controller.m_buffer.dMin = dMin;
            controller.m_buffer.dMax = dMax;
            controller.m_buffer.fireTick =
                controller.resolveFireTick(effectTick, delay, delay, true);
            controller.m_buffer.fireTick =
                std::max(controller.m_buffer.fireTick, effectTick + dMin);
            controller.m_buffer.fireTick =
                std::min(controller.m_buffer.fireTick, effectTick + dMax);
            EbAssistLog::debug(fmt::format("bake orb-heuristic win=[{},{}] ticks", dMin, dMax));
        }
    }
    controller.m_buffer.fireTick = std::min(
        controller.m_buffer.fireTick, controller.m_buffer.pressTick + controller.m_buffer.limit);

    EbAssistLog::info(
        fmt::format("bake ASSIST {} +{} ticks (window [{},{}] nomF={} "
                    "eT={} nT={} lT={} effectT={} fire={} where={} spatial={} approxF={})",
                    down ? "press" : "release", delay, win->earliest, win->latest,
                    win->nominalFrame, earliestTick, nominalTick, latestTick, effectTick,
                    controller.m_buffer.fireTick, armWhere, spatialIn ? 1 : 0, approxFrame));
    {
        EbDevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = false;
        controller.m_buffer.devPressId = EbDevCycleRecorder::get().noteDelayArm(
            AssistReason::DelayPrefer, armWhere, delay, controller.m_buffer.fireTick, 0,
            DevEventSource::Bake, meta);
    }
    controller.noteVerdict(AssistReason::DelayPrefer, armWhere);
    EbAssistRealMetrics::get().noteArm();
    controller.updateOverlay();
    return false;
}
