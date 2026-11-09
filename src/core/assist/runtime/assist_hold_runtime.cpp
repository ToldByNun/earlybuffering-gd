#include "assist_hold_runtime.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../dev/capture/dev_trace_store.hpp"
#include "../../diagnostics/assist_real_metrics.hpp"
#include "../../learning/level_profile.hpp"
#include "../../sim/foresight/travel_foresight.hpp"
#include "../../sim/planning/simulation_plan.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../../sim/sim_trust.hpp"
#include "../assist_controller.hpp"
#include "../contracts/assist_verdict.hpp"
#include "../contracts/miss_assist_contracts.hpp"
#include <algorithm>

bool EbAssistHoldRuntime::armHoldUntilSafe(EbAssistController& controller, PlayLayer* playLayer,
                                         PlayerObject* player, bool player1, int effectTick,
                                         int limit, EbSimPlan const& plan, bool down) {
    (void)playLayer;
    if (!player || limit <= 0)
        return true; // fall through vanilla
    // Commit 13: HUS nur Trust.
    if (!EbSimTrust::get().okAt(player->getPositionX()))
        return true;

    int const eta = controller.holdUntilSafeEtaTicks(plan, limit);
    controller.m_buffer = {};
    controller.m_buffer.state = BufferState::Buffered;
    controller.m_buffer.down = down;
    controller.m_buffer.player1 = player1;
    controller.m_buffer.pressTick = effectTick;
    controller.m_buffer.limit = limit;
    controller.m_buffer.planDelay = eta;
    controller.m_buffer.fireTick = effectTick + eta;
    controller.m_buffer.expectedJumpTick = controller.m_buffer.fireTick;
    controller.m_buffer.continuous = plan.continuous;
    controller.m_buffer.decisionX = player->getPositionX();
    controller.m_buffer.mode = EbStageConfig::kindOf(player);
    controller.m_buffer.requireOrb = plan.requireOrb || plan.foresightOrbPad || plan.usedOrbWindow;
    controller.m_buffer.requireHold = plan.requireHold;
    controller.m_buffer.releaseAfter = plan.releaseAfter;
    controller.m_buffer.releaseEarliest = plan.releaseEarliest;
    controller.m_buffer.releaseLatest = plan.releaseLatest;
    controller.m_buffer.dMin = plan.dMin;
    controller.m_buffer.dMax = plan.dMax;
    controller.m_buffer.planId = plan.planId;
    controller.m_buffer.holdUntilSafe = true;
    controller.m_buffer.husLastReplanTick = -1; // force first cadence replan
    controller.m_buffer.husLastReplanX = controller.m_buffer.decisionX;
    controller.m_buffer.husLastForesight = plan.foresightOrbPad || plan.usedOrbWindow;
    controller.m_buffer.fireTick = std::min(controller.m_buffer.fireTick, effectTick + limit);

    controller.m_outcomePlanDelay = eta > 0;
    controller.m_outcomeExpectOrb = controller.m_buffer.requireOrb;
    {
        EbDevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = plan.budgetHit;
        controller.m_buffer.devPressId = EbDevCycleRecorder::get().noteDelayArm(
            AssistReason::HoldPending, "hus-arm", eta, controller.m_buffer.fireTick, plan.planId,
            DevEventSource::Live, meta);
    }
    controller.noteVerdict(AssistReason::HoldPending, "hus-arm");
    if (eta > 0)
        EbAssistRealMetrics::get().noteArm();
    EbAssistLog::debug(fmt::format(
        "HOLD-UNTIL-SAFE arm {} eta={} limit={} foresight={} orbWin={} budgetHit={} id={}",
        down ? "press" : "release", eta, limit, plan.foresightOrbPad ? 1 : 0,
        plan.usedOrbWindow ? 1 : 0, plan.budgetHit ? 1 : 0, plan.planId));
    controller.recordDecision(playLayer, player, down, plan, effectTick, eta, limit);
    controller.updateOverlay();
    return false; // swallow
}

void EbAssistHoldRuntime::tickHoldUntilSafe(EbAssistController& controller, PlayLayer* playLayer,
                                          PlayerObject* player) {
    if (!playLayer || !player)
        return;
    if (controller.m_buffer.state != BufferState::Buffered)
        return;
    if (!controller.m_buffer.holdUntilSafe)
        return;
    if (controller.m_simDt <= 0.f)
        return;

    // Commit 13: trust lost mid-HUS → Pass (drop soft hold, no inject).
    if (!EbSimTrust::get().okAt(player->getPositionX())) {
        EbAssistLog::debug("HOLD-UNTIL-SAFE abort (trust lost) → Pass");
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, AssistReason::PassUntrust,
                                        "hus-trust-lost", controller.m_attemptTick);
        controller.noteVerdict(AssistReason::PassUntrust, "hus-trust-lost");
        controller.clearBuffer();
        return;
    }

    int const deadline = controller.m_buffer.pressTick + controller.m_buffer.limit;
    int const remaining = deadline - controller.m_attemptTick;

    // Never wait past the window — fire at deadline.
    if (remaining <= 0 || controller.m_attemptTick >= deadline) {
        EbAssistLog::debug(fmt::format("HOLD-UNTIL-SAFE deadline fire @{} (press={} limit={})",
                                     controller.m_attemptTick, controller.m_buffer.pressTick,
                                     controller.m_buffer.limit));
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, AssistReason::HoldPending,
                                        "hus-deadline", controller.m_attemptTick);
        controller.m_buffer.holdUntilSafe = false;
        controller.m_buffer.fireTick = controller.m_attemptTick;
        controller.fireBuffered(playLayer, player, false);
        return;
    }

    int const remLimit = std::max(1, remaining);
    float const x = player->getPositionX();
    // Cheap foresight edge (no full plan) so cadence can wake on orb/pad enter/leave.
    int const slack = EbStageConfig::deathSlack();
    bool const foresightNow =
        EbStageConfig::assistOrbs() &&
        EbTravelForesight::hasOrbOrPadAhead(playLayer, player, remLimit, controller.m_simDt, slack);

    if (!controller.shouldReplanHoldUntilSafe(controller.m_attemptTick,
                                              controller.m_buffer.husLastReplanTick, x,
                                              controller.m_buffer.husLastReplanX, foresightNow,
                                              controller.m_buffer.husLastForesight, remaining)) {
        controller.m_buffer.fireTick = deadline;
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, AssistReason::HoldPending,
                                        "hus-skip", controller.m_buffer.fireTick);
        controller.noteVerdict(AssistReason::HoldPending, "hus-skip");
        EbLagProbe::get().mark("hus.replan.skip",
                             fmt::format("t={} rem={}", controller.m_attemptTick, remaining));
        return;
    }

    // Never nest a full plan inside PlayerObject::update (Eclipse PauseBGLHook
    // coexistence / iOS ring SIGBUS). Physics-tick HUS already runs outside update;
    // this guards any future mid-step call site.
    if (controller.m_inRealUpdate) {
        controller.m_buffer.fireTick = deadline;
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, AssistReason::HoldPending,
                                        "hus-defer-mid-update", controller.m_buffer.fireTick);
        controller.noteVerdict(AssistReason::HoldPending, "hus-defer-mid-update");
        EbLagProbe::get().mark("hus.replan.defer",
                             fmt::format("t={} rem={}", controller.m_attemptTick, remaining));
        return;
    }

    EbLagProbe::Scope const husScope(
        "eb.hus.replan", fmt::format("rem={} foresight={}", remaining, foresightNow ? 1 : 0));

    EbPlanOptions opts;
    opts.budgetUs = EbStageConfig::husReplanBudgetUs();
    opts.tickBudget = EbStageConfig::husReplanTickBudget();
    opts.capMul =
        EbStageConfig::autoTune() ? EbLevelProfile::get().capMul(controller.m_buffer.mode) : 1.f;
    opts.maxBranches = 2;
    opts.smartDepth = 1;
    opts.legacyPick = EbStageConfig::assistPickLegacy();
    opts.assistEngine = EbStageConfig::effectiveAssistEngine();
    opts.attemptStartTick = controller.m_attemptTick;
    opts.requireAliveForDelay = true;
    EbAssistControllerImplementation::fillPlanRobustLatency(opts, player, controller.m_dtVarEma,
                                                          true);

    bool const husDown = controller.m_buffer.down;
    auto re = EbShadowSimulator::get().plan(playLayer, player, husDown, remLimit, controller.m_simDt,
                                          controller.m_simDt, opts);

    controller.m_buffer.husLastReplanTick = controller.m_attemptTick;
    controller.m_buffer.husLastReplanX = x;
    controller.m_buffer.husLastForesight = foresightNow || re.foresightOrbPad || re.usedOrbWindow;

    if (re.action == PlanAction::Delay && re.delay > 0) {
        int delay = EbMissAssistContracts::clampDelayToWindow(re.delay, remLimit);
        if (delay < 1)
            delay = 1;
        int const jumpRel = re.jumpTick > 0 ? re.jumpTick : delay;
        int proposed = controller.resolveFireTick(controller.m_attemptTick, delay, jumpRel,
                                                  re.requireOrb || controller.m_buffer.requireOrb);
        proposed = std::min(proposed, deadline);
        if (proposed < controller.m_attemptTick)
            proposed = controller.m_attemptTick;

        // Prefer not sliding earlier than current tentative fire unless orb window.
        if (!re.requireOrb && !controller.m_buffer.requireOrb &&
            proposed < controller.m_buffer.fireTick && controller.m_buffer.fireTick <= deadline) {
            proposed = controller.m_buffer.fireTick;
        }

        controller.m_buffer.fireTick = proposed;
        controller.m_buffer.planDelay = EbMissAssistContracts::clampDelayToWindow(
            controller.m_attemptTick - controller.m_buffer.pressTick + delay,
            controller.m_buffer.limit);
        controller.m_buffer.expectedJumpTick = controller.m_attemptTick + jumpRel;
        controller.m_buffer.requireOrb = controller.m_buffer.requireOrb || re.requireOrb;
        controller.m_buffer.requireHold = re.requireHold;
        controller.m_buffer.releaseAfter = re.releaseAfter;
        controller.m_buffer.releaseEarliest = re.releaseEarliest;
        controller.m_buffer.releaseLatest = re.releaseLatest;
        if (re.dMin >= 0)
            controller.m_buffer.dMin = re.dMin;
        if (re.dMax >= 0)
            controller.m_buffer.dMax = re.dMax;
        controller.m_buffer.holdUntilSafe = false; // hard Delay confirmed
        AssistReason delayReason = re.reason;
        if (!EbAssistVerdict::isDelay(delayReason)) {
            delayReason = EbAssistVerdict::inferDelayReason(re.activatedRing, re.requireOrb, false);
        }
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, delayReason, "hus-confirm",
                                        controller.m_buffer.fireTick);
        controller.noteVerdict(delayReason, "hus-confirm");
        EbAssistLog::debug(fmt::format("HOLD-UNTIL-SAFE confirm Delay +{} fire={} deadline={}", delay,
                                     controller.m_buffer.fireTick, deadline));
        return;
    }

    // Opportunity gone and now looks safe → inject now (never drop the click).
    bool const clearOpp = !re.foresightOrbPad && !re.usedOrbWindow && !re.budgetHit;
    bool const safeNow = re.reason == AssistReason::PassNoThreat ||
                         re.reason == AssistReason::PassD0Safe ||
                         (re.action == PlanAction::Pass && re.nowIsSafe && clearOpp);
    if (safeNow) {
        EbAssistLog::debug(fmt::format("HOLD-UNTIL-SAFE flush Pass now @{} reason={}",
                                     controller.m_attemptTick, EbAssistVerdict::name(re.reason)));
        controller.m_buffer.holdUntilSafe = false;
        controller.m_buffer.fireTick = controller.m_attemptTick;
        controller.fireBuffered(playLayer, player, false);
        return;
    }

    // Budget exhaust / still uncertain: park at deadline (never hang).
    // PassBudget on HUS does not drop the click — window-cap fire later.
    controller.m_buffer.fireTick = deadline;
    if (re.budgetHit || re.reason == AssistReason::PassBudget) {
        EbLagProbe::get().mark("hus.replan.budget",
                             fmt::format("t={} rem={}", controller.m_attemptTick, remaining));
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, AssistReason::HoldPending,
                                        "hus-budget", controller.m_buffer.fireTick);
        controller.noteVerdict(AssistReason::HoldPending, "hus-budget");
        EbAssistLog::debug(
            fmt::format("HOLD-UNTIL-SAFE budget exhaust → deadline={} (never hang)", deadline));
    } else {
        EbDevCycleRecorder::get().noteHus(controller.m_buffer.devPressId, AssistReason::HoldPending,
                                        "hus-wait", controller.m_buffer.fireTick);
        controller.noteVerdict(AssistReason::HoldPending, "hus-wait");
    }
}
