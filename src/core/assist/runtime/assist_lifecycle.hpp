#ifndef ASSIST_LIFECYCLE_HPP
#define ASSIST_LIFECYCLE_HPP

#include "../assist_controller.hpp"

class EbAssistLifecycle {
public:
    static void syncButtonState(EbAssistController& controller, PlayLayer* playLayer);
    static geode::Result<> ensureStartSnapshot(EbAssistController& controller, PlayLayer* layer);
    static geode::Result<int> runBakeMap(EbAssistController& controller, PlayLayer* layer,
                                         bool bruteForce);
    static void resetLevelProfile(EbAssistController& controller);
    static void beginRealUpdate(EbAssistController& controller);
    static void endRealUpdate(EbAssistController& controller);
    static void onSubStep(EbAssistController& controller, PlayerObject* player, float deltaTime);

private:
    EbAssistLifecycle() = delete;
};

#endif
