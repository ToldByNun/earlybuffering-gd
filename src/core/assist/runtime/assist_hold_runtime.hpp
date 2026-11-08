#ifndef ASSIST_HOLD_RUNTIME_HPP
#define ASSIST_HOLD_RUNTIME_HPP

#include "../../sim/planning/simulation_plan.hpp"

#include "../assist_controller.hpp"

class AssistHoldRuntime {
public:
    static bool armHoldUntilSafe(AssistController& controller, PlayLayer* playLayer,
                                 PlayerObject* player, bool player1, int effectTick, int limit,
                                 SimPlan const& plan, bool down);
    static void tickHoldUntilSafe(AssistController& controller, PlayLayer* playLayer,
                                  PlayerObject* player);

private:
    AssistHoldRuntime() = delete;
};

#endif
