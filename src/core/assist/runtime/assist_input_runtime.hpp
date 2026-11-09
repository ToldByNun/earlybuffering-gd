#ifndef ASSIST_INPUT_RUNTIME_HPP
#define ASSIST_INPUT_RUNTIME_HPP

#include "../assist_controller.hpp"

class EbAssistInputRuntime {
public:
    static void onPhysicsTick(EbAssistController& controller, PlayLayer* playLayer, float deltaTime);
    static bool onHandleButton(EbAssistController& controller, GJBaseGameLayer* layer, bool down,
                               int button, bool player1);
    static bool allowPushButton(EbAssistController& controller, PlayerObject* player,
                                PlayerButton button);

private:
    EbAssistInputRuntime() = delete;
};

#endif
