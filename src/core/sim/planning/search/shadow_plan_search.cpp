#include "shadow_plan_search.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../debug/lag_probe.hpp"
#include "../../../../debug/perf_trace.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../assist/contracts/assist_verdict.hpp"
#include "../../../assist/contracts/live_plan_contracts.hpp"
#include "../../../assist/planning/assist_horizon.hpp"
#include "../shadow_survival_rules.hpp"
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

EbSimPlan EbShadowPlanSearch::run(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                              bool down, int limit, float deltaTime, float firstDt,
                              EbPlanOptions const& options) {
    EbPerfTrace::Scope const scope("plan");
    static int s_planId = 1;
    EbSimPlan plan;
    plan.smartDepthUsed = 1;
    plan.planId = s_planId++;
    if (!layer || !real || !simulator.m_shadow || limit < 0 || deltaTime <= 0.f) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassInvalidArgs;
        if (limit >= 0) {
            EbDevRejectLedger::BuildInput emptyIn;
            emptyIn.limit = limit;
            emptyIn.doPass = true;
            emptyIn.chosenDelay = 0;
            emptyIn.includeScores = EbDevConfig::recordCands();
            auto ledger = EbDevRejectLedger::build(emptyIn);
            plan.candNotes = std::move(ledger.notes);
            plan.candNotesTruncated = ledger.truncated;
        }
        return plan;
    }
    if (firstDt <= 0.f || firstDt > deltaTime)
        firstDt = deltaTime;

    EbShadowPlanContext context(simulator, layer, real, down, limit, deltaTime, firstDt, options,
                              plan);
    simulator.m_simTickBase = std::max(0, options.attemptStartTick);
    simulator.m_simAbsTick = -1;
    auto const captureStart = std::chrono::steady_clock::now();
    EbSimSessionGuard session(simulator, layer);
    auto const prepareStart = std::chrono::steady_clock::now();
    plan.worldCaptureUs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(prepareStart - captureStart).count());
    prepare(context);
    plan.prepareUs = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - prepareStart)
                                          .count());
    if (EbShadowPlanInputWindow::evaluate(context)) {
        if (plan.plannerPath.empty())
            plan.plannerPath = "inputWindow";
        return plan;
    }
    bool const trustLong = context.real && EbSimTrust::get().okAt(context.real->getPositionX());
    auto const horizonCaps = EbAssistHorizon::resolve(
        context.real ? EbStageConfig::kindOf(context.real) : GameModeKind::Cube, trustLong,
        context.limit, context.options.capMul, 0, EbStageConfig::lookAheadCap());
    context.minSurviveLong =
        EbAssistHorizon::minSurviveTicksForSuccess(horizonCaps.scoreHorizon, trustLong);

    context.requireAlive = context.options.requireAliveForDelay;

    context.scoreHorizon = horizonCaps.scoreHorizon;

    if (EbShadowPlanThreatProbe::evaluate(context)) {
        if (plan.plannerPath.empty())
            plan.plannerPath = "threatProbe";
        return plan;
    }
    EbShadowPlanCandidateSearch::evaluate(context);
    if (EbShadowPlanSelection::choose(context)) {
        if (plan.plannerPath.empty())
            plan.plannerPath = "classic";
        return plan;
    }
    EbShadowPlanResult::publish(context);
    if (plan.plannerPath.empty())
        plan.plannerPath = "classic";
    return plan;
}

void EbShadowPlanSearch::prepare(EbShadowPlanContext& context) {
    context.sawToggleUnknown = false;
    context.hadToggleTargets = !context.simulator.m_toggleTargetIds.empty();

    bool const continuousControl =
        context.real->m_isDart || context.real->m_isShip || context.real->m_isSwing;
    context.inputWindow =
        context.options.requireAliveForDelay && (context.down || continuousControl);
    context.model.stopAtBallLanding = context.inputWindow && context.real->m_isBall &&
                                      !context.useTrajEngine && !context.linkedDual();
    // Continuous IW: max(local look-ahead, short AssistHorizon planCap) so Wave
    // keeps ≥32 ticks (AP3a) instead of bare limit. Discrete retain 96-tick max.
    // Explicit mode/look-ahead settings take precedence. Prove ticks follow.
    if (context.inputWindow && EbStageConfig::paramsFor(context.real).cap == 0 &&
        EbStageConfig::lookAheadCap() == 0) {
        if (continuousControl) {
            auto const kind = EbStageConfig::kindOf(context.real);
            context.model.cap = EbAssistHorizon::continuousInputWindowCap(
                kind, context.limit, context.options.capMul);
            if (context.options.requireAliveForDelay && context.model.cap > 0)
                context.model.cap += EbShadowSurvivalRules::kPostCapProveTicks;
        } else {
            context.model.cap = std::min(context.model.cap, std::max(1, 96 - context.limit));
        }
    }
    // Assist Pack AP-L2: keep soft-wall headroom for IW — baseline/orb scans must
    // not burn the whole press (Grief TW baseline alone was ~15ms / candidatesTried=0).
    int const prepareReserve = EbLivePlanContracts::prepareReserveUs(
        context.options.budgetUs, context.inputWindow && continuousControl);
    bool const wallUnlimited = context.options.budgetUs <= 0;
    auto prepareHeavyOk = [&]() -> bool {
        if (context.wallExceeded()) {
            context.plan.budgetHit = true;
            return false;
        }
        return EbLivePlanContracts::prepareHeavyWorkAllowed(context.wallRemUs(), prepareReserve,
                                                            wallUnlimited);
    };

    // Classic always coasts. Continuous Live IW also runs a cheap baseline so
    // idleSafer can steer Wave help-only / futile (AP3b) without leaving IW.
    if (EbLivePlanContracts::waveIwBaselineCoastAllowed(context.inputWindow, continuousControl,
                                                        context.down) &&
        prepareHeavyOk()) {
        auto base = context.simulator.runBaseline(context.layer, context.real, context.down,
                                                  context.deltaTime, context.firstDt, context.model,
                                                  &context.tickBudget);
        context.simulator.resetCaptureForNextRun(context.layer);
        if (context.wallExceeded())
            context.plan.budgetHit = true;
        if (base.died) {
            context.plan.baselineDeathTick = base.deathTick;
            context.plan.baselineDeathObject = base.deathObject;
            context.plan.baselineDeathX = base.deathX;
        }
    }

    context.orbAssist = EbStageConfig::assistOrbs() && context.down;
    context.orbHint = {};
    context.spatialHint = false;
    // AP4: spatial hint always when orbs on (even under Live IW). Cheap — only
    // skip when the soft wall is already gone (not the prepare reserve).
    if (context.orbAssist &&
        EbLivePlanContracts::intendedSpatialOrbHintAllowed(context.orbAssist) &&
        !context.wallExceeded()) {
        context.spatialHint = context.simulator.spatialOrbHint(context.layer, context.real,
                                                               context.limit, context.deltaTime);
        context.plan.foresightOrbPad = context.spatialHint;
    }
    bool const orbScanBudgetOk =
        context.tickBudget >= EbOpportunityProbe::kMinBudgetForProbe && prepareHeavyOk();
    if (context.orbAssist &&
        EbLivePlanContracts::intendedOrbWindowScanAllowed(context.orbAssist, context.inputWindow,
                                                          orbScanBudgetOk)) {
        int const scanTicks = EbOpportunityProbe::orbScanTicks(context.limit, context.model.slack);
        context.orbHint = context.simulator.scanOrbWindow(
            context.layer, context.real, context.down, scanTicks, context.deltaTime,
            context.firstDt, &context.tickBudget, false);
        context.simulator.resetCaptureForNextRun(context.layer);
        if (context.wallExceeded())
            context.plan.budgetHit = true;
        // Press-to-reach: second scan with Jump held if coast missed (budget-capped).
        if (!context.orbHint.found &&
            context.tickBudget >= EbOpportunityProbe::kMinBudgetForProbe && prepareHeavyOk()) {
            int const reachTicks =
                EbOpportunityProbe::pressReachScanTicks(context.limit, context.model.slack);
            auto held = context.simulator.scanOrbWindow(context.layer, context.real, context.down,
                                                        reachTicks, context.deltaTime,
                                                        context.firstDt, &context.tickBudget, true);
            context.simulator.resetCaptureForNextRun(context.layer);
            if (context.wallExceeded())
                context.plan.budgetHit = true;
            if (held.found) {
                context.orbHint = held;
            }
        }
        if (context.orbHint.found)
            context.plan.usedOrbWindow = true;
    }

    context.zone = {};
    context.cands.resize(static_cast<size_t>(context.limit) + 1);
    context.candReady.assign(static_cast<size_t>(context.limit) + 1, 0);

    // Adaptive fallthrough seeds: place terminal/horizon results before probes
    // so threat/candidate search skip re-sim. finalizeViable runs after zone.
    for (auto const& seeded : context.options.seedCandidates) {
        if (seeded.delay < 0 || seeded.delay > context.limit)
            continue;
        size_t const index = static_cast<size_t>(seeded.delay);
        context.cands[index] = seeded;
        context.candReady[index] = 1;
        if (seeded.toggleUnknown || (seeded.unknownTrunc && context.hadToggleTargets))
            context.sawToggleUnknown = true;
    }

    context.ledgerActMin = -1;
    context.ledgerActMax = -1;
    context.ledgerOrbIntent = false;
    context.ledgerPreferEarlyExit = false;
    context.ledgerSameCrashExit = false;
    context.ledgerEarliestSafeExit = false;
    context.ledgerPeakDeathExit = false;
}
