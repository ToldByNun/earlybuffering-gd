#ifndef HOLD_UNTIL_SAFE_POLICY_HPP
#define HOLD_UNTIL_SAFE_POLICY_HPP

#include "../../sim/planning/simulation_plan.hpp"
#include <string>

class HoldUntilSafePolicy {
public:
    static bool shouldArmHoldUntilSafe(SimPlan const& plan, bool trustOk);
    static int holdUntilSafeEtaTicks(SimPlan const& plan, int limit);
    static bool holdUntilSafeSelfCheck(std::string& report);
    static bool shouldReplanHoldUntilSafe(int attemptTick, int lastReplanTick, float x,
                                          float lastReplanX, bool foresightNow, bool foresightLast,
                                          int remainingTicks);

private:
    HoldUntilSafePolicy() = delete;
};

#endif
