#include "assist_buffer_runtime.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../ui/feedback/save_feedback.hpp"
#include "../../learning/level_profile.hpp"
#include "../../sim/planning/simulation_plan.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../assist_controller.hpp"
#include <algorithm>

void AssistBufferRuntime::inject(AssistController& controller, GJBaseGameLayer* layer, bool down) {
    if (!layer)
        return;
    controller.m_assistTouchedRun = true;
    if (down) {
        ++controller.m_savesAttempt;
        ++controller.m_savesLevel;
    }
    // Timeline already seeded in recordDecision with the planned fire tick.
    controller.m_injecting = true;
    layer->handleButton(down, AssistControllerImplementation::JUMP_BUTTON,
                        controller.m_buffer.player1);
    controller.m_injecting = false;
}

void AssistBufferRuntime::flushPendingRelease(AssistController& controller, PlayLayer* playLayer) {
    if (!controller.m_pendingRelease || !playLayer)
        return;

    bool const playerOne = controller.m_pendingReleaseP1;
    bool const holding = controller.userHolding(playerOne);
    auto* player = playerOne ? playLayer->m_player1 : playLayer->m_player2;

    // HARD RULE: while the user still holds Jump, never inject a release.
    if (holding) {
        return;
    }

    // First physics tick after user-up: start the extend budget (max X ticks).
    if (controller.m_pendingReleaseUserUpTick < 0) {
        controller.m_pendingReleaseUserUpTick = controller.m_attemptTick;
        controller.m_pendingReleaseTick =
            controller.m_pendingReleaseUserUpTick + controller.m_pendingReleaseMaxExtend;
        AssistLog::debug(fmt::format(
            "hold-assist: user-up @{} extend≤{} deadline={}", controller.m_pendingReleaseUserUpTick,
            controller.m_pendingReleaseMaxExtend, controller.m_pendingReleaseTick));
    }

    bool gameDown = false;
    if (player) {
        auto it = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        gameDown = it != player->m_holdingButtons.end() && it->second;
    }
    if (!gameDown) {
        controller.m_pendingRelease = false;
        controller.m_pendingReleaseHold = false;
        controller.m_pendingReleaseContinuous = false;
        controller.m_pendingReleaseUserUpTick = -1;
        controller.m_pendingReleaseMaxExtend = 0;
        return;
    }

    // Still inside the post-release extend window — keep Jump down.
    if (controller.m_attemptTick < controller.m_pendingReleaseTick) {
        return;
    }

    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_injecting = true;
    playLayer->handleButton(false, AssistControllerImplementation::JUMP_BUTTON, playerOne);
    controller.m_injecting = false;
    AssistLog::debug(fmt::format("hold-assist: injected release @{} (p{}) [extend done]",
                                 controller.m_attemptTick, playerOne ? 1 : 2));
}

void AssistBufferRuntime::fireBuffered(AssistController& controller, PlayLayer* playLayer,
                                       PlayerObject* player, bool allowRecheck) {
    int const elapsed = controller.m_attemptTick - controller.m_buffer.pressTick;
    int const remaining = controller.m_buffer.limit - elapsed;

    // Soft path (physics due / recheck): never inject before fireTick.
    // Force paths (CPS / mode-change / HUS deadline) pass allowRecheck=false.
    if (allowRecheck && controller.m_buffer.fireTick > controller.m_attemptTick && remaining > 0) {
        AssistLog::debug(fmt::format("fireBuffered: not due yet attempt={} fire={} — wait",
                                     controller.m_attemptTick, controller.m_buffer.fireTick));
        return;
    }

    // Orb/window-aware recheck: stay inside [dMin, dMax]; never slide late into death.
    // Skipped for CPS / mode-change force-fire.
    if (allowRecheck && !controller.m_buffer.fixedFireTick && player && remaining > 0 &&
        controller.m_buffer.rechecks < 2 && controller.m_simDt > 0.f) {
        int const absMin = controller.m_buffer.dMin >= 0
                               ? controller.m_buffer.pressTick + controller.m_buffer.dMin
                               : controller.m_buffer.pressTick;
        int const absMax = controller.m_buffer.dMax >= 0
                               ? controller.m_buffer.pressTick + controller.m_buffer.dMax
                               : controller.m_buffer.pressTick + controller.m_buffer.limit;
        int const deadline = controller.m_buffer.pressTick + controller.m_buffer.limit;
        int const robustK = controller.estimateRobustK(player, controller.m_dtVarEma);

        if (controller.m_attemptTick < absMin) {
            // Too early for orb window — wait (orb dMin force; not slide-capped).
            int const oldFire = controller.m_buffer.fireTick;
            controller.m_buffer.fireTick = absMin;
            ++controller.m_buffer.rechecks;
            DevCycleRecorder::get().noteRecheck(controller.m_buffer.devPressId, oldFire, absMin,
                                                "orb-dmin");
            if (StageConfig::debugLogging() || StageConfig::perfTrace()) {
                auto const detail =
                    fmt::format("recheck: oldFire={}→newFire={} (orb-dMin wait tick={})", oldFire,
                                absMin, controller.m_attemptTick);
                AssistLog::debug(detail);
                PerfTrace::get().note("recheck", detail);
            }
            return;
        }
        if (controller.m_attemptTick > absMax && controller.m_buffer.requireOrb) {
            // Past window — fire immediately
        } else if (!controller.m_buffer.requireOrb || controller.m_attemptTick <= absMax) {
            PlanOptions opts;
            opts.budgetUs = StageConfig::simBudgetUs();
            opts.tickBudget = StageConfig::tickBudgetPerPress() / 4;
            opts.attemptStartTick = controller.m_attemptTick;
            opts.capMul = StageConfig::autoTune()
                              ? LevelProfile::get().capMul(controller.m_buffer.mode)
                              : 1.f;
            opts.maxBranches = 2;
            opts.smartDepth = 1;
            opts.legacyPick = StageConfig::assistPickLegacy();
            opts.assistEngine = StageConfig::effectiveAssistEngine();
            opts.requireAliveForDelay = true;
            AssistControllerImplementation::fillPlanRobustLatency(opts, player,
                                                                  controller.m_dtVarEma, true);
            auto re =
                ShadowSimulator::get().plan(playLayer, player, controller.m_buffer.down, remaining,
                                            controller.m_simDt, controller.m_simDt, opts);

            // Surviving Delay only — never slide into a lethal / hopeless replan.
            bool const planOk = re.action == PlanAction::Delay && re.delay > 0 && !re.budgetHit &&
                                re.threatCleared &&
                                (re.requireOrb || controller.m_buffer.requireOrb || !re.unknown);

            if (planOk) {
                int const jumpRel = re.jumpTick > 0 ? re.jumpTick : re.delay;
                int proposed = controller.resolveFireTick(controller.m_attemptTick, re.delay,
                                                          jumpRel, controller.m_buffer.requireOrb);
                if (controller.m_buffer.dMin >= 0) {
                    proposed = std::max(proposed, absMin);
                }
                if (controller.m_buffer.dMax >= 0) {
                    proposed = std::min(proposed, absMax);
                }
                proposed = std::min(proposed, deadline);

                int const oldFire = controller.m_buffer.fireTick;
                int const accepted = controller.acceptRecheckSlide(
                    oldFire, controller.m_attemptTick, proposed, absMax, robustK, deadline,
                    controller.m_buffer.requireOrb, true);
                if (accepted > 0) {
                    ++controller.m_buffer.rechecks;
                    controller.m_buffer.fireTick = accepted;
                    controller.m_buffer.expectedJumpTick = controller.m_attemptTick + jumpRel;
                    controller.m_buffer.planDelay = re.delay;
                    DevCycleRecorder::get().noteRecheck(controller.m_buffer.devPressId, oldFire,
                                                        accepted, "slide");
                    if (StageConfig::debugLogging() || StageConfig::perfTrace()) {
                        auto const detail =
                            fmt::format("recheck: oldFire={}→newFire={} jump={} Δ={} (tick={})",
                                        oldFire, accepted, controller.m_buffer.expectedJumpTick,
                                        accepted - oldFire, controller.m_attemptTick);
                        AssistLog::debug(detail);
                        PerfTrace::get().note("recheck", detail);
                    }
                    return;
                }
            }
        }
    }

    bool const wasPress = controller.m_buffer.down;
    bool const player1 = controller.m_buffer.player1;
    bool const requireHold = controller.m_buffer.requireHold;
    bool const continuous = controller.m_buffer.continuous;
    int const releaseAfter = controller.m_buffer.releaseAfter;
    int const releaseEarliest = controller.m_buffer.releaseEarliest;
    int const releaseLatest = controller.m_buffer.releaseLatest;
    bool const letGo = controller.m_buffer.releaseSeen || !controller.userHolding(player1);
    int const delayed = std::max(0, controller.m_attemptTick - controller.m_buffer.pressTick);
    int const planId = controller.m_buffer.planId;
    bool const expectOrb = controller.m_buffer.requireOrb;
    int64_t const devPressId = controller.m_buffer.devPressId;
    // Max post-release / queued hold = this press's tick window (e.g. Ticks=4 → ≤4).
    int const extendCap = std::max(0, controller.m_buffer.limit);
    int const queuedHold = controller.resolveQueuedHoldTicks(
        requireHold, continuous, releaseAfter, releaseEarliest, releaseLatest, extendCap);

    DevCycleRecorder::get().noteFire(devPressId, controller.m_attemptTick);

    controller.inject(playLayer, controller.m_buffer.down);
    // Real-harm watch: death after a fired Delay inject (Commit 2 DAF).
    if (delayed > 0 && wasPress) {
        controller.m_dafWatch = true;
    }
    AssistLog::printSavedPress(delayed, controller.m_buffer.pressTick, controller.m_attemptTick,
                               controller.m_buffer.limit, controller.estimatedTps());
    if (delayed > 0 && (StageConfig::saveFeedbackSound() || StageConfig::saveFeedbackIcon() ||
                        StageConfig::saveFeedbackCounter())) {
        SaveFeedback::get().onSave(playLayer, player, delayed);
    }
    if (!wasPress) {
        AssistLog::debug(fmt::format("(assisted RELEASE, tick={})", controller.m_attemptTick));
    }

    controller.m_outcomePending = true;
    controller.m_outcomePlanId = planId;
    controller.m_outcomeUntil = controller.m_attemptTick + 96;
    controller.m_outcomePlayer1 = player1;
    controller.m_outcomeExpectOrb = expectOrb;
    controller.m_outcomePlanDelay = delayed > 0;
    controller.m_outcomeStartX = player ? player->getPositionX() : 0.f;

    controller.clearBuffer();

    // After assisted press:
    // - While user holds: never force-release.
    // - User already up (releaseSeen) + plan hold → queued hold in GD for N ticks, then UP.
    // - User already up + plain Delay → ASAP release (swallowed early UP).
    // - User still holding + requireHold → extend-after-release on their UP.
    if (wasPress) {
        if (letGo) {
            controller.m_pendingRelease = true;
            controller.m_pendingReleaseP1 = player1;
            controller.m_pendingReleaseArmTick = controller.m_attemptTick;
            controller.m_pendingReleaseUserUpTick = controller.m_attemptTick;
            if (queuedHold > 0) {
                // Queued click hold: inject stays down for planned ticks despite early UP.
                controller.m_pendingReleaseHold = requireHold || releaseAfter > 0;
                controller.m_pendingReleaseContinuous = continuous;
                controller.m_pendingReleaseMaxExtend = queuedHold;
                controller.m_pendingReleaseTick = controller.m_attemptTick + queuedHold;
                AssistLog::debug(fmt::format(
                    "hold-assist: arm queued hold={} @fire={} hold={} cont={} rel=[{},{}]@{}",
                    queuedHold, controller.m_attemptTick, requireHold ? 1 : 0, continuous ? 1 : 0,
                    releaseEarliest, releaseLatest, releaseAfter));
                DevCycleRecorder::get().noteHoldExtend(devPressId, queuedHold);
            } else {
                controller.m_pendingReleaseHold = false;
                controller.m_pendingReleaseContinuous = false;
                controller.m_pendingReleaseMaxExtend = 0;
                controller.m_pendingReleaseTick = controller.m_attemptTick; // flush ASAP
                AssistLog::debug(
                    fmt::format("hold-assist: arm release ASAP (user already up) @fire={}",
                                controller.m_attemptTick));
            }
        } else if (requireHold) {
            // Only explicit requireHold (dash orb / robot hold) gets post-release extend.
            // Continuous modes (Wave/Ship/Swing) plan releases live in planRelease so
            // letting go never drags the player into ceiling hazards for +limit ticks.
            int const extend = queuedHold > 0 ? queuedHold : extendCap;
            controller.m_pendingRelease = true;
            controller.m_pendingReleaseP1 = player1;
            controller.m_pendingReleaseHold = requireHold;
            controller.m_pendingReleaseContinuous = continuous;
            controller.m_pendingReleaseArmTick = controller.m_attemptTick;
            controller.m_pendingReleaseUserUpTick = -1;
            controller.m_pendingReleaseMaxExtend = extend;
            controller.m_pendingReleaseTick = 0; // set on user-up
            AssistLog::debug(fmt::format(
                "hold-assist: arm extend-after-release max={} @fire={} hold={} cont={}", extend,
                controller.m_attemptTick, requireHold ? 1 : 0, continuous ? 1 : 0));
            DevCycleRecorder::get().noteHoldExtend(devPressId, extend);
        }
        // else: plain Delay tap — user owns hold+release fully (no pending).
    }
}
