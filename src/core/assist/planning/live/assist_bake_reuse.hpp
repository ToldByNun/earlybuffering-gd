#ifndef ASSIST_BAKE_REUSE_HPP
#define ASSIST_BAKE_REUSE_HPP

#include "../../../bake/bake_verifier.hpp"

#include "../../assist_controller.hpp"

class AssistBakeReuse {
public:
    static bool tryBakeAssist(AssistController& controller, PlayLayer* playLayer,
                              PlayerObject* player, bool player1, bool down, int effectTick,
                              float firstDt, bool& handled, PressSimBudget* sharedBudget,
                              bool* verifyAttempted, HybridPressReuse* reuse);

private:
    AssistBakeReuse() = delete;
};

#endif
