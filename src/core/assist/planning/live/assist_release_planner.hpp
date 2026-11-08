#ifndef ASSIST_RELEASE_PLANNER_HPP
#define ASSIST_RELEASE_PLANNER_HPP

#include "../../../../dev/capture/dev_clock_contract.hpp"

#include "../../assist_controller.hpp"

class AssistReleasePlanner {
public:
    static bool planRelease(AssistController& controller, PlayLayer* playLayer,
                            PlayerObject* player, bool player1, int effectTick, float firstDt,
                            DevClockContract::Stamp const& clockIn);

private:
    AssistReleasePlanner() = delete;
};

#endif
