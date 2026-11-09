#ifndef ASSIST_BAKE_REUSE_HPP
#define ASSIST_BAKE_REUSE_HPP

#include "../../../bake/bake_verifier.hpp"

#include "../../assist_controller.hpp"

class EbAssistBakeReuse {
public:
    static bool tryBakeAssist(EbAssistController& controller, PlayLayer* playLayer,
                              PlayerObject* player, bool player1, bool down, int effectTick,
                              float firstDt, bool& handled, EbPressSimBudget* sharedBudget,
                              bool* verifyAttempted, EbHybridPressReuse* reuse);

private:
    EbAssistBakeReuse() = delete;
};

#endif
