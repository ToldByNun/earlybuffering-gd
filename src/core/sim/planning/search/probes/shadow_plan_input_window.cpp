#include "shadow_plan_input_window.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../../simulation_plan.hpp"
#include "../shadow_plan_context.hpp"

bool EbShadowPlanInputWindow::evaluate(EbShadowPlanContext& context) {
    if (!context.inputWindow)
        return false;

    context.plan.fixedFireTick = true;
    auto const complete = [&](EbShadowSimulator::RunResult const& result) {
        return EbMissAssistContracts::survivesObservation(
            result.died, result.unknown || result.unknownTrunc || result.toggleUnknown,
            result.ticksRan, context.limit + context.model.cap, result.landedAfterAction);
    };
    bool const dual = context.linkedDual();
    // Continuous: earliest-safe only (local steering window). Discrete ball/cube
    // must scan the full delay band — earliest Alive is often worse than the
    // survive_then_endx peak (Grief: Live d=2 while Eval bestSurv=5).
    bool const continuous =
        context.real &&
        (context.real->m_isDart || context.real->m_isShip || context.real->m_isSwing);

    context.plan.reason = AssistReason::PassHopeless;
    context.plan.viableDelays.clear();

    int bestDelay = -1;
    int bestSurviveBin = -1;
    float bestScore = 0.f;

    auto considerSafe = [&](int delayValue, EbSimulationCandidate const& candidate) {
        if (delayValue > 0)
            context.plan.viableDelays.push_back(delayValue);

        if (continuous) {
            if (bestDelay >= 0)
                return;
            bestDelay = delayValue;
            return;
        }

        int const surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(candidate.raw);
        int const surviveBin = EbShadowSurvivalRules::surviveBinOf(surviveTicks);
        float const score =
            candidate.endX - context.latencyWeight * static_cast<float>(delayValue);
        if (bestDelay < 0 || surviveBin > bestSurviveBin ||
            (surviveBin == bestSurviveBin && score > bestScore)) {
            bestDelay = delayValue;
            bestSurviveBin = surviveBin;
            bestScore = score;
        }
    };

    for (int delayValue = 0; delayValue <= context.limit; ++delayValue) {
        if (context.wallExceeded() || context.tickBudget <= 0) {
            context.plan.budgetHit = true;
            break;
        }

        auto candidate = context.evalDelay(delayValue, -1);
        if (candidate.raw.ticksRan == 0) {
            context.plan.budgetHit = true;
            break;
        }
        bool dead = candidate.raw.died;
        bool safe = complete(candidate.raw);
        if (dual && !dead) {
            auto* other = context.real == context.layer->m_player1 ? context.layer->m_player2
                                                                   : context.layer->m_player1;
            if (context.wallExceeded() || context.tickBudget <= 0) {
                context.plan.budgetHit = true;
                break;
            }
            auto partner = context.runEngine(other, delayValue, -1, nullptr);
            context.simulator.resetCaptureForNextRun(context.layer);
            dead = partner.died;
            safe = safe && complete(partner);
        }
        candidate.viable = safe;
        candidate.clearsThreat = safe;
        candidate.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(candidate.raw);
        candidate.surviveBin = EbShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
        context.cands[static_cast<size_t>(delayValue)] = candidate;
        context.candReady[static_cast<size_t>(delayValue)] = 1;
        if (delayValue == 0) {
            context.plan.firstDeathTick = candidate.raw.deathTick;
            context.plan.firstDeathObject = candidate.raw.deathObject;
            if (!dead && !safe) {
                context.plan.reason = AssistReason::PassHardUnknown;
                break;
            }
        }
        if (!safe)
            continue;

        considerSafe(delayValue, candidate);
        // Continuous: stop at earliest observation-safe delay.
        if (continuous && bestDelay >= 0)
            break;
    }

    if (bestDelay >= 0) {
        context.plan.delay = context.plan.jumpTick = bestDelay;
        context.plan.nowIsSafe = bestDelay == 0;
        context.plan.threatCleared = true;
        context.plan.reason =
            bestDelay ? AssistReason::DelayThreat : AssistReason::PassD0Safe;
        if (bestDelay)
            context.plan.action = PlanAction::Delay;
        else
            context.plan.action = PlanAction::Pass;
        if (!context.plan.viableDelays.empty()) {
            context.plan.dMin = context.plan.viableDelays.front();
            context.plan.dMax = context.plan.viableDelays.back();
        }
    }

    if (context.plan.budgetHit && bestDelay < 0)
        context.plan.reason = AssistReason::PassBudget;

    context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                          context.plan.budgetHit ? EbDevRejectLedger::EarlyExit::Budget
                                                 : EbDevRejectLedger::EarlyExit::None);
    return true;
}
