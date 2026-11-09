#include "shadow_plan_input_window.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../simulation_plan.hpp"
#include "../shadow_plan_context.hpp"

bool EbShadowPlanInputWindow::evaluate(EbShadowPlanContext& context) {
    if (context.inputWindow) {
        context.plan.fixedFireTick = true;
        auto const complete = [&](EbShadowSimulator::RunResult const& result) {
            return EbMissAssistContracts::survivesObservation(
                result.died, result.unknown || result.unknownTrunc || result.toggleUnknown,
                result.ticksRan, context.limit + context.model.cap, result.landedAfterAction);
        };
        bool const dual = context.linkedDual();
        context.plan.reason = AssistReason::PassHopeless;
        for (int delayValue = 0; delayValue <= context.limit; ++delayValue) {
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
            context.cands[delayValue] = candidate;
            context.candReady[delayValue] = 1;
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
            context.plan.delay = context.plan.jumpTick = delayValue;
            context.plan.nowIsSafe = delayValue == 0;
            context.plan.threatCleared = true;
            context.plan.reason = delayValue ? AssistReason::DelayThreat : AssistReason::PassD0Safe;
            if (delayValue) {
                context.plan.action = PlanAction::Delay;
                context.plan.viableDelays.push_back(delayValue);
            }
            break;
        }
        if (context.plan.budgetHit)
            context.plan.reason = AssistReason::PassBudget;
        context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                              context.plan.budgetHit ? EbDevRejectLedger::EarlyExit::Budget
                                                     : EbDevRejectLedger::EarlyExit::None);
        return true;
    }

    return false;
}
