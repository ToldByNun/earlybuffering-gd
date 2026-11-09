#ifndef HOLD_UNTIL_SAFE_POLICY_HPP
#define HOLD_UNTIL_SAFE_POLICY_HPP

#include "../../sim/planning/simulation_plan.hpp"
#include <string>

class EbHoldUntilSafePolicy {
public:
    static bool shouldArmHoldUntilSafe(EbSimPlan const& plan, bool trustOk);
    static int holdUntilSafeEtaTicks(EbSimPlan const& plan, int limit);
    static bool holdUntilSafeSelfCheck(std::string& report);
    static bool shouldReplanHoldUntilSafe(int attemptTick, int lastReplanTick, float x,
                                          float lastReplanX, bool foresightNow, bool foresightLast,
                                          int remainingTicks);

private:
    EbHoldUntilSafePolicy() = delete;
};

#endif
