#ifndef ASSIST_RELEASE_PLANNER_HPP
#define ASSIST_RELEASE_PLANNER_HPP

#include "../../../../dev/capture/dev_clock_contract.hpp"

#include "../../assist_controller.hpp"

class EbAssistReleasePlanner {
public:
    static bool planRelease(EbAssistController& controller, PlayLayer* playLayer,
                            PlayerObject* player, bool player1, int effectTick, float firstDt,
                            EbDevClockContract::Stamp const& clockIn);

private:
    EbAssistReleasePlanner() = delete;
};

#endif
