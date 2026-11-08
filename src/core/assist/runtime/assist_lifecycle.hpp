#ifndef ASSIST_LIFECYCLE_HPP
#define ASSIST_LIFECYCLE_HPP

#include "../assist_controller.hpp"

class AssistLifecycle {
public:
    static void onCheckpointLoad(AssistController& controller);
    static void onLevelComplete(AssistController& controller, PlayLayer* playLayer);
    static void syncButtonState(AssistController& controller, PlayLayer* playLayer);
    static void resetAttempt(AssistController& controller, PlayLayer* playLayer);
    static void onLevelInit(AssistController& controller, PlayLayer* playLayer);
    static void onLevelQuit(AssistController& controller);
    static geode::Result<> ensureStartSnapshot(AssistController& controller, PlayLayer* layer);
    static geode::Result<int> runBakeMap(AssistController& controller, PlayLayer* layer,
                                         bool bruteForce);
    static void resetLevelProfile(AssistController& controller);
    static void beginRealUpdate(AssistController& controller);
    static void endRealUpdate(AssistController& controller);
    static void onSubStep(AssistController& controller, PlayerObject* player, float deltaTime);

private:
    AssistLifecycle() = delete;
};

#endif
