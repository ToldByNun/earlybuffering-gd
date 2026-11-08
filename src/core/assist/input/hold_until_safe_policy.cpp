#include "hold_until_safe_policy.hpp"
#include "../../sim/planning/simulation_plan.hpp"
#include "../assist_controller.hpp"
#include "../contracts/assist_verdict.hpp"
#include "../contracts/miss_assist_contracts.hpp"
#include <algorithm>
#include <cmath>
#include <string>

bool HoldUntilSafePolicy::shouldArmHoldUntilSafe(SimPlan const& plan, bool trustOk) {
    // Commit 13: HUS only when trusted — sonst Pass.
    if (!trustOk)
        return false;
    if (plan.action == PlanAction::Delay)
        return false;
    if (plan.reason == AssistReason::PassBudget)
        return true;
    bool const opp = plan.foresightOrbPad || plan.usedOrbWindow;
    if (plan.budgetHit && opp)
        return true;
    if (plan.unknown && opp)
        return true;
    // Continuous: idle safer + ascent helped in eval but pick starved → soft hold.
    if (plan.continuous && plan.idleSafer && plan.ascentUseful)
        return true;
    // Hopeless after a real search: Soft Hold + replan (trusted only).
    if (plan.reason == AssistReason::PassHopeless)
        return true;
    (void)opp;
    return false;
}

int HoldUntilSafePolicy::holdUntilSafeEtaTicks(SimPlan const& plan, int limit) {
    if (limit <= 0)
        return 0;
    int eta = 0;
    if (plan.dMin > 0) {
        eta = plan.dMin;
    } else if (plan.delay > 0) {
        eta = plan.delay;
    } else {
        eta = std::max(1, limit / 2);
    }
    eta = MissAssistContracts::clampDelayToWindow(eta, limit);
    if (eta < 1)
        eta = 1;
    return eta;
}

bool HoldUntilSafePolicy::holdUntilSafeSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    SimPlan budgetPass;
    budgetPass.action = PlanAction::Pass;
    budgetPass.reason = AssistReason::PassBudget;
    expect(AssistController::shouldArmHoldUntilSafe(budgetPass, true), "arm-budget");
    expect(!AssistController::shouldArmHoldUntilSafe(budgetPass, false), "no-hus-untrust");

    SimPlan delayOk;
    delayOk.action = PlanAction::Delay;
    delayOk.delay = 4;
    delayOk.reason = AssistReason::DelayThreat;
    expect(!AssistController::shouldArmHoldUntilSafe(delayOk, true), "no-arm-delay");

    SimPlan noThreat;
    noThreat.action = PlanAction::Pass;
    noThreat.reason = AssistReason::PassNoThreat;
    expect(!AssistController::shouldArmHoldUntilSafe(noThreat, true), "no-arm-no-threat");

    SimPlan foresightBudget;
    foresightBudget.action = PlanAction::Pass;
    foresightBudget.reason = AssistReason::PassHopeless;
    foresightBudget.budgetHit = true;
    foresightBudget.foresightOrbPad = true;
    expect(AssistController::shouldArmHoldUntilSafe(foresightBudget, true), "arm-foresight-budget");

    SimPlan hopelessBare;
    hopelessBare.action = PlanAction::Pass;
    hopelessBare.reason = AssistReason::PassHopeless;
    expect(AssistController::shouldArmHoldUntilSafe(hopelessBare, true), "arm-hopeless");

    SimPlan unknownOrb;
    unknownOrb.action = PlanAction::Pass;
    unknownOrb.reason = AssistReason::PassD0Safe;
    unknownOrb.unknown = true;
    unknownOrb.usedOrbWindow = true;
    expect(AssistController::shouldArmHoldUntilSafe(unknownOrb), "arm-unknown-orb");

    // Toggle-unknown + foresight → Soft Hold OK; plain toggle Pass → no HUS.
    SimPlan toggleUnk;
    toggleUnk.action = PlanAction::Pass;
    toggleUnk.reason = AssistReason::PassToggleUnknown;
    toggleUnk.unknown = true;
    toggleUnk.toggleUnknown = true;
    expect(!AssistController::shouldArmHoldUntilSafe(toggleUnk), "no-arm-toggle-plain");
    toggleUnk.foresightOrbPad = true;
    expect(AssistController::shouldArmHoldUntilSafe(toggleUnk), "arm-toggle-foresight");

    SimPlan idleAscent;
    idleAscent.action = PlanAction::Pass;
    idleAscent.reason = AssistReason::PassD0Safe;
    idleAscent.continuous = true;
    idleAscent.idleSafer = true;
    idleAscent.ascentUseful = true;
    expect(AssistController::shouldArmHoldUntilSafe(idleAscent, true), "arm-idle-ascent");
    idleAscent.continuous = false;
    expect(!AssistController::shouldArmHoldUntilSafe(idleAscent, true), "no-arm-idle-cube");

    SimPlan etaPlan;
    etaPlan.dMin = 6;
    expect(AssistController::holdUntilSafeEtaTicks(etaPlan, 4) == 4, "eta-clamp-6-to-4");
    expect(AssistController::holdUntilSafeEtaTicks(etaPlan, 16) == 6, "eta-dmin-6");
    SimPlan midPlan;
    expect(AssistController::holdUntilSafeEtaTicks(midPlan, 16) == 8, "eta-mid-16");
    expect(AssistController::holdUntilSafeEtaTicks(midPlan, 0) == 0, "eta-limit0");

    // Cadence: first call / interval / ΔX / foresight edge / near deadline.
    expect(AssistController::shouldReplanHoldUntilSafe(10, -1, 0.f, 0.f, false, false, 8),
           "replan-first");
    expect(!AssistController::shouldReplanHoldUntilSafe(11, 10, 100.f, 100.f, false, false, 8),
           "replan-skip-1");
    expect(!AssistController::shouldReplanHoldUntilSafe(12, 10, 100.f, 100.f, false, false, 8),
           "replan-skip-2");
    expect(AssistController::shouldReplanHoldUntilSafe(13, 10, 100.f, 100.f, false, false, 8),
           "replan-every-3");
    expect(AssistController::shouldReplanHoldUntilSafe(11, 10, 110.f, 100.f, false, false, 8),
           "replan-delta-x");
    expect(AssistController::shouldReplanHoldUntilSafe(11, 10, 100.f, 100.f, true, false, 8),
           "replan-foresight-edge");
    expect(AssistController::shouldReplanHoldUntilSafe(11, 10, 100.f, 100.f, false, false, 2),
           "replan-near-deadline");
    expect(AssistController::kHusReplanEveryTicks == 3, "hus-every-const");
    expect(AssistController::kHusReplanDeltaX == 8.f, "hus-dx-const");

    if (fails == 0) {
        report = "holdUntilSafeSelfCheck OK";
        return true;
    }
    report = fmt::format("holdUntilSafeSelfCheck {} fails: {}", fails, report);
    return false;
}

bool HoldUntilSafePolicy::shouldReplanHoldUntilSafe(int attemptTick, int lastReplanTick, float x,
                                                    float lastReplanX, bool foresightNow,
                                                    bool foresightLast, int remainingTicks) {
    if (lastReplanTick < 0)
        return true;
    if (remainingTicks <= 2)
        return true;
    if (foresightNow != foresightLast)
        return true;
    if (std::fabs(x - lastReplanX) >= AssistController::kHusReplanDeltaX)
        return true;
    if (!(attemptTick - lastReplanTick >= AssistController::kHusReplanEveryTicks)) {
        return false;
    }
    return true;
}
