#ifndef ADAPTIVE_PARITY_POLICY_HPP
#define ADAPTIVE_PARITY_POLICY_HPP

#include "../../../../assist/contracts/assist_verdict.hpp"
#include <string>

// Pure Adaptive → classic fallthrough rules (no Geode / no physics).
// PlanGold + tools/test_adaptive_parity_smoke.cpp lock these so Live Assist
// cannot silently stop falling through (simUs=0 Hopeless regression).
class EbAdaptiveParityPolicy {
public:
    // Early Adaptive PassHopeless (same-crash / verify wipe) → classic.
    static bool shouldFallThroughEarlyPass(AssistReason reason);

    // Gate: Delay never falls through. Pass sticks only for proven PassD0Safe.
    // PassBudget / PassHopeless / other Pass → classic.
    // isDelayAction == (PlanAction::Delay); keep this Geode-free for offline smoke.
    static bool shouldFallThroughGate(bool isDelayAction, AssistReason reason);

    static bool selfCheck(std::string& report);

private:
    EbAdaptiveParityPolicy() = delete;
};

#endif
