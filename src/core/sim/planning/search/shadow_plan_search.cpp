#include "shadow_plan_search.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../debug/lag_probe.hpp"
#include "../../../../debug/perf_trace.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../assist/contracts/assist_verdict.hpp"
#include "../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../../assist/planning/assist_horizon.hpp"
#include "../../../assist/planning/opportunity_probe.hpp"
#include "../../shadow_simulator.hpp"
#include "../../sim_session_guard.hpp"
#include "../../sim_trust.hpp"
#include "../simulation_plan.hpp"
#include "probes/shadow_plan_input_window.hpp"
#include "probes/shadow_plan_threat_probe.hpp"
#include "selection/shadow_plan_result.hpp"
#include "selection/shadow_plan_selection.hpp"
#include "shadow_plan_candidate_search.hpp"
#include "shadow_plan_context.hpp"
#include <algorithm>

SimPlan ShadowPlanSearch::run(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                              bool down, int limit, float deltaTime, float firstDt,
                              PlanOptions const& options) {
    PerfTrace::Scope const scope("plan");
    static int s_planId = 1;
    SimPlan plan;
    plan.smartDepthUsed = 1;
    plan.planId = s_planId++;
    if (!layer || !real || !simulator.m_shadow || limit < 0 || deltaTime <= 0.f) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassInvalidArgs;
        if (limit >= 0) {
            DevRejectLedger::BuildInput emptyIn;
            emptyIn.limit = limit;
            emptyIn.doPass = true;
            emptyIn.chosenDelay = 0;
            emptyIn.includeScores = DevConfig::recordCands();
            auto ledger = DevRejectLedger::build(emptyIn);
            plan.candNotes = std::move(ledger.notes);
            plan.candNotesTruncated = ledger.truncated;
        }
        return plan;
    }
    if (firstDt <= 0.f || firstDt > deltaTime)
        firstDt = deltaTime;

    ShadowPlanContext context(simulator, layer, real, down, limit, deltaTime, firstDt, options,
                              plan);
    simulator.m_simTickBase = std::max(0, options.attemptStartTick);
    simulator.m_simAbsTick = -1;
    SimSessionGuard session(simulator, layer);
    prepare(context);
    if (ShadowPlanInputWindow::evaluate(context))
        return plan;
    bool const trustLong = context.real && SimTrust::get().okAt(context.real->getPositionX());
    auto const horizonCaps = AssistHorizon::resolve(
        context.real ? StageConfig::kindOf(context.real) : GameModeKind::Cube, trustLong,
        context.limit, context.options.capMul, 0, StageConfig::lookAheadCap());
    context.minSurviveLong =
        AssistHorizon::minSurviveTicksForSuccess(horizonCaps.scoreHorizon, trustLong);

    context.requireAlive = context.options.requireAliveForDelay;

    context.scoreHorizon = horizonCaps.scoreHorizon;

    if (ShadowPlanThreatProbe::evaluate(context))
        return plan;
    ShadowPlanCandidateSearch::evaluate(context);
    if (ShadowPlanSelection::choose(context))
        return plan;
    ShadowPlanResult::publish(context);
    return plan;
}

void ShadowPlanSearch::prepare(ShadowPlanContext& context) {
    context.sawToggleUnknown = false;
    context.hadToggleTargets = !context.simulator.m_toggleTargetIds.empty();

    bool const continuousControl =
        context.real->m_isDart || context.real->m_isShip || context.real->m_isSwing;
    context.inputWindow =
        context.options.requireAliveForDelay && (context.down || continuousControl);
    context.model.stopAtBallLanding = context.inputWindow && context.real->m_isBall &&
                                      !context.useTrajEngine && !context.linkedDual();
    // Continuous steering is local to the input window: a single held direction
    // cannot cover the later turns of a corridor. Discrete modes retain the
    // existing 96-tick maximum. Explicit horizon settings take precedence.
    if (context.inputWindow && StageConfig::paramsFor(context.real).cap == 0 &&
        StageConfig::lookAheadCap() == 0) {
        if (continuousControl) {
            context.model.cap = MissAssistContracts::continuousLookAhead(context.limit);
        } else {
            context.model.cap = std::min(context.model.cap, std::max(1, 96 - context.limit));
        }
    }
    if (!context.inputWindow) {
        auto base = context.simulator.runBaseline(context.layer, context.real, context.down,
                                                  context.deltaTime, context.firstDt, context.model,
                                                  &context.tickBudget);
        context.simulator.resetCaptureForNextRun(context.layer);
        if (base.died) {
            context.plan.baselineDeathTick = base.deathTick;
            context.plan.baselineDeathObject = base.deathObject;
            context.plan.baselineDeathX = base.deathX;
        }
    }

    context.orbAssist = StageConfig::assistOrbs() && context.down;
    context.orbHint = {};
    context.spatialHint = false;
    if (context.orbAssist && !context.inputWindow) {
        int const scanTicks = OpportunityProbe::orbScanTicks(context.limit, context.model.slack);
        context.orbHint = context.simulator.scanOrbWindow(
            context.layer, context.real, context.down, scanTicks, context.deltaTime,
            context.firstDt, &context.tickBudget, false);
        context.simulator.resetCaptureForNextRun(context.layer);
        // Press-to-reach: second scan with Jump held if coast missed (budget-capped).
        if (!context.orbHint.found && context.tickBudget >= OpportunityProbe::kMinBudgetForProbe) {
            int const reachTicks =
                OpportunityProbe::pressReachScanTicks(context.limit, context.model.slack);
            auto held = context.simulator.scanOrbWindow(context.layer, context.real, context.down,
                                                        reachTicks, context.deltaTime,
                                                        context.firstDt, &context.tickBudget, true);
            context.simulator.resetCaptureForNextRun(context.layer);
            if (held.found) {
                context.orbHint = held;
            }
        }
        if (context.orbHint.found)
            context.plan.usedOrbWindow = true;
        context.spatialHint = context.simulator.spatialOrbHint(context.layer, context.real,
                                                               context.limit, context.deltaTime);
        context.plan.foresightOrbPad = context.spatialHint;
    }

    context.zone = {};
    context.cands.resize(static_cast<size_t>(context.limit) + 1);
    context.candReady.assign(static_cast<size_t>(context.limit) + 1, 0);

    context.ledgerActMin = -1;
    context.ledgerActMax = -1;
    context.ledgerOrbIntent = false;
    context.ledgerPreferEarlyExit = false;
}
