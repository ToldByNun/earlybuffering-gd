#include "shadow_plan_result.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../simulation_plan.hpp"
#include "../shadow_plan_context.hpp"

void EbShadowPlanResult::publish(EbShadowPlanContext& context) {
    if (context.chosen < 0) {
        context.plan.action = PlanAction::Pass;
        context.plan.toggleUnknown = context.sawToggleUnknown;
        context.plan.unknown = context.sawToggleUnknown;
        context.plan.reason =
            context.sawToggleUnknown
                ? AssistReason::PassToggleUnknown
                : (context.plan.budgetHit ? AssistReason::PassBudget : AssistReason::PassHopeless);
        context.plan.delay = 0;
        EbAssistLog::debug(fmt::format(
            "PLAN id={} Pass verdict={} limit={} budgetHit={} toggleUnk={} orbIntent={}",
            context.plan.planId, EbAssistVerdict::name(context.plan.reason), context.limit,
            context.plan.budgetHit, context.sawToggleUnknown ? 1 : 0, context.orbIntent));
        EbDevRejectLedger::EarlyExit early = EbDevRejectLedger::EarlyExit::None;
        if (context.plan.budgetHit)
            early = EbDevRejectLedger::EarlyExit::Budget;
        else if (context.ledgerPreferEarlyExit)
            early = EbDevRejectLedger::EarlyExit::PreferBand;
        context.publishLedger(0, true, early);
        return;
    }

    if (context.doPass || context.chosen == 0) {
        context.plan.action = PlanAction::Pass;
        context.plan.reason = AssistReason::PassD0Safe;
        context.plan.delay = 0;
        context.plan.nowIsSafe = context.cands[0].viable;
        context.plan.jumpTick = context.cands[0].jumpTick;
        context.plan.activatedRing = context.cands[0].activatedRing;
        context.plan.toggleUnknown = context.sawToggleUnknown || context.cands[0].toggleUnknown;
        context.plan.unknown = context.plan.toggleUnknown || context.cands[0].unknownTrunc;
        // d0 "safe" that only looked clear through unknown → honesty Pass.
        if (context.cands[0].unknownTrunc || context.cands[0].toggleUnknown ||
            !context.cands[0].viable) {
            if (context.sawToggleUnknown || context.cands[0].toggleUnknown ||
                context.hadToggleTargets) {
                context.plan.reason = AssistReason::PassToggleUnknown;
                context.plan.toggleUnknown = true;
                context.plan.unknown = true;
            } else if (context.cands[0].unknownTrunc) {
                // Commit 11: Hard Unknown leftover → named Pass (never Delay).
                context.plan.reason = AssistReason::PassHardUnknown;
                context.plan.unknown = true;
            }
        }
    } else {
        auto const& winCand = context.cands[static_cast<size_t>(context.chosen)];
        // Belt: never ship Delay on unknown/toggle-unknown path.
        if (winCand.unknownTrunc || winCand.toggleUnknown) {
            context.plan.action = PlanAction::Pass;
            context.plan.delay = 0;
            context.plan.unknown = true;
            context.plan.nowIsSafe = false;
            if (winCand.toggleUnknown || context.hadToggleTargets) {
                context.plan.reason = AssistReason::PassToggleUnknown;
                context.plan.toggleUnknown = true;
            } else {
                // Commit 11: Hard Unknown → pass-hard-unknown.
                context.plan.reason = AssistReason::PassHardUnknown;
                context.plan.toggleUnknown = false;
            }
        } else {
            context.plan.action = PlanAction::Delay;
            context.plan.reason =
                EbAssistVerdict::inferDelayReason(context.plan.activatedRing || winCand.activatedRing,
                                                context.plan.requireOrb, context.hasPrefer);
            context.plan.delay = context.chosen;
            context.plan.jumpTick = winCand.jumpTick;
            context.plan.nowIsSafe = false;
            context.plan.activatedRing = context.plan.activatedRing || winCand.activatedRing;
            // Chosen path is clean — other cands may have hit toggle-unknown.
            context.plan.unknown = false;
            context.plan.toggleUnknown = false;
            if (context.plan.releaseAfter < 0 && winCand.releaseAfter >= 0) {
                context.plan.releaseAfter = winCand.releaseAfter;
                context.plan.releaseEarliest = context.plan.releaseAfter;
                context.plan.releaseLatest = context.plan.releaseAfter;
            }
            context.plan.requireHold = context.plan.requireHold || winCand.requireHold;
            // Non-orb: publish survivor band for overlay / prefer.
            if (!context.plan.requireOrb) {
                int sLo = -1;
                int sHi = -1;
                for (int delay = 0; delay <= context.limit; ++delay) {
                    auto const& candidate = context.cands[static_cast<size_t>(delay)];
                    if (!candidate.viable)
                        continue; // Commit 9: Landing may set raw.died
                    if (sLo < 0)
                        sLo = delay;
                    sHi = delay;
                }
                if (sLo >= 0) {
                    context.plan.dMin = sLo;
                    context.plan.dMax = sHi;
                }
            }
        }
    }

    // Commit 8: exact viable delays in window (exit-predict must not fill gaps).
    {
        context.plan.viableDelays.clear();
        context.plan.viableDelays.reserve(static_cast<size_t>(context.limit) + 1);
        for (int delay = 0; delay <= context.limit; ++delay) {
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            if (!candidate.viable)
                continue;
            context.plan.viableDelays.push_back(delay);
        }
    }

    // Dash orbs: sim keeps Jump down via tapRelease exceptions, but live inject
    // used to default releaseAfter=1 and cancel the dash. Force hold when the
    // winning Delay cand dashed.
    if (context.plan.action == PlanAction::Delay && context.chosen >= 0 &&
        context.chosen <= context.limit) {
        auto const& win = context.cands[static_cast<size_t>(context.chosen)];
        if (win.raw.wasDashing) {
            context.plan.requireHold = true;
            context.plan.activatedRing = true;
            if (context.plan.releaseAfter < 40)
                context.plan.releaseAfter = 40;
            if (context.plan.releaseEarliest < 0 ||
                context.plan.releaseEarliest > context.plan.releaseAfter) {
                context.plan.releaseEarliest = context.plan.releaseAfter;
            }
            if (context.plan.releaseLatest < 120)
                context.plan.releaseLatest = 120;
        }
    }

    // Final honesty: never ship Delay when the *chosen* path is unknown.
    if (context.plan.action == PlanAction::Delay &&
        (context.plan.unknown || context.plan.toggleUnknown)) {
        context.plan.action = PlanAction::Pass;
        context.plan.delay = 0;
        context.plan.unknown = true;
        context.plan.requireHold = false;
        if (context.plan.toggleUnknown) {
            context.plan.reason = AssistReason::PassToggleUnknown;
            EbAssistLog::debug(
                fmt::format("PLAN id={} refuse Delay → pass-toggle-unknown", context.plan.planId));
        } else {
            // Commit 11: Hard Unknown → named Pass.
            context.plan.reason = AssistReason::PassHardUnknown;
            EbAssistLog::debug(
                fmt::format("PLAN id={} refuse Delay → pass-hard-unknown", context.plan.planId));
        }
    }

    // Commit 11: fully evaluated Delay stays armed even if later search hit budget.
    // Soft-trust partial pick is blocked above (budget-partial + softTrustPartial).

    if (context.plan.action == PlanAction::Delay && context.plan.reason == AssistReason::None) {
        context.plan.reason = EbAssistVerdict::inferDelayReason(
            context.plan.activatedRing, context.plan.requireOrb, context.hasPrefer);
    }
    if (context.plan.action == PlanAction::Pass && context.plan.reason == AssistReason::None) {
        context.plan.reason = AssistReason::PassUnknown;
    }

    bool const orbIntentOut =
        context.plan.requireOrb || context.plan.activatedRing || context.plan.usedOrbWindow;
    EbAssistLog::debug(fmt::format(
        "PLAN id={} {} verdict={} delay={} orbIntent={} foresight={} idleSafer={} ascent={} eng={} "
        "ring={} useful={} hold={} rel=[{},{}]@{} scan={} dMin={} dMax={} tried={} budgetLeft={}",
        context.plan.planId, context.plan.action == PlanAction::Pass ? "Pass" : "Delay",
        EbAssistVerdict::name(context.plan.reason), context.plan.delay, orbIntentOut ? 1 : 0,
        context.plan.foresightOrbPad ? 1 : 0, context.plan.idleSafer ? 1 : 0,
        context.plan.ascentUseful ? 1 : 0,
        context.plan.trajectoryEngine ? "Trajectory" : "ShadowPlayer", context.plan.activatedRing,
        context.plan.orbUseful, context.plan.requireHold, context.plan.releaseEarliest,
        context.plan.releaseLatest, context.plan.releaseAfter, context.plan.holdScanTried,
        context.plan.dMin, context.plan.dMax, context.plan.candidatesTried, context.tickBudget));
    {
        EbDevRejectLedger::EarlyExit early = EbDevRejectLedger::EarlyExit::None;
        if (context.plan.budgetHit)
            early = EbDevRejectLedger::EarlyExit::Budget;
        else if (context.ledgerPreferEarlyExit)
            early = EbDevRejectLedger::EarlyExit::PreferBand;
        bool const pass = (context.plan.action == PlanAction::Pass);
        context.publishLedger(pass ? 0 : context.plan.delay, pass, early);
    }
    return;
}
