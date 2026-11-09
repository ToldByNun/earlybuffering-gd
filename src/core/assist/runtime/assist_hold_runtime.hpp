#ifndef ASSIST_HOLD_RUNTIME_HPP
#define ASSIST_HOLD_RUNTIME_HPP

#include "../../sim/planning/simulation_plan.hpp"

#include "../assist_controller.hpp"

class EbAssistHoldRuntime {
public:
    static bool armHoldUntilSafe(EbAssistController& controller, PlayLayer* playLayer,
                                 PlayerObject* player, bool player1, int effectTick, int limit,
                                 EbSimPlan const& plan, bool down);
    static void tickHoldUntilSafe(EbAssistController& controller, PlayLayer* playLayer,
                                  PlayerObject* player);

private:
    EbAssistHoldRuntime() = delete;
};

#endif
