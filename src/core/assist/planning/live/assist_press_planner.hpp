#ifndef ASSIST_PRESS_PLANNER_HPP
#define ASSIST_PRESS_PLANNER_HPP

#include "../../../../dev/capture/dev_clock_contract.hpp"

#include "../../assist_controller.hpp"

class AssistPressPlanner {
public:
    static bool planPress(AssistController& controller, PlayLayer* playLayer, PlayerObject* player,
                          bool player1, int effectTick, float firstDt,
                          DevClockContract::Stamp const& clockIn);

private:
    AssistPressPlanner() = delete;
};

#endif
