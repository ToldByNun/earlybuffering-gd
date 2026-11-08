#ifndef ASSIST_INPUT_RUNTIME_HPP
#define ASSIST_INPUT_RUNTIME_HPP

#include "../assist_controller.hpp"

class AssistInputRuntime {
public:
    static void onPhysicsTick(AssistController& controller, PlayLayer* playLayer, float deltaTime);
    static bool onHandleButton(AssistController& controller, GJBaseGameLayer* layer, bool down,
                               int button, bool player1);
    static bool allowPushButton(AssistController& controller, PlayerObject* player,
                                PlayerButton button);

private:
    AssistInputRuntime() = delete;
};

#endif
