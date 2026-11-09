#include "adaptive_parity_policy.hpp"

bool EbAdaptiveParityPolicy::shouldFallThroughEarlyPass(AssistReason reason) {
    return reason == AssistReason::PassHopeless;
}

bool EbAdaptiveParityPolicy::shouldFallThroughGate(bool isDelayAction, AssistReason reason) {
    if (isDelayAction)
        return false;
    // Proven d0-safe is Adaptive-terminal; everything else needs classic.
    return reason != AssistReason::PassD0Safe;
}

bool EbAdaptiveParityPolicy::selfCheck(std::string& report) {
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

    expect(shouldFallThroughEarlyPass(AssistReason::PassHopeless), "early-hopeless-ft");
    expect(!shouldFallThroughEarlyPass(AssistReason::PassD0Safe), "early-d0-no-ft");
    expect(!shouldFallThroughEarlyPass(AssistReason::PassBudget), "early-budget-no-ft");
    expect(!shouldFallThroughEarlyPass(AssistReason::None), "early-none-no-ft");

    expect(shouldFallThroughGate(false, AssistReason::PassBudget), "gate-budget-ft");
    expect(shouldFallThroughGate(false, AssistReason::PassHopeless), "gate-hopeless-ft");
    expect(shouldFallThroughGate(false, AssistReason::PassNoThreat), "gate-other-pass-ft");
    expect(!shouldFallThroughGate(false, AssistReason::PassD0Safe), "gate-d0-no-ft");
    expect(!shouldFallThroughGate(true, AssistReason::DelayThreat), "gate-delay-no-ft");
    expect(!shouldFallThroughGate(true, AssistReason::PassHopeless), "gate-delay-action-no-ft");

    if (fails == 0)
        report = "AdaptiveParityPolicy OK";
    else
        report = std::string("AdaptiveParityPolicy fails: ") + report;
    return fails == 0;
}
