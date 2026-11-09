#ifndef ASSIST_PRESS_PLANNER_HPP
#define ASSIST_PRESS_PLANNER_HPP

#include "../../../../dev/capture/dev_clock_contract.hpp"

#include "../../assist_controller.hpp"

class EbAssistPressPlanner {
public:
    static bool planPress(EbAssistController& controller, PlayLayer* playLayer, PlayerObject* player,
                          bool player1, int effectTick, float firstDt,
                          EbDevClockContract::Stamp const& clockIn);

private:
    EbAssistPressPlanner() = delete;
};

#endif
