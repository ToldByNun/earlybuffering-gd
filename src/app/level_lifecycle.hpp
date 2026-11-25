#ifndef LEVEL_LIFECYCLE_HPP
#define LEVEL_LIFECYCLE_HPP
#include <chrono>

class EbTransitionPhases {
public:
    explicit EbTransitionPhases(char const* transition);
    void mark(char const* phase);

private:
    char const* m_transition;
    std::chrono::steady_clock::time_point m_started;
};

class EbAssistController;
class PlayLayer;

// Ordered cross-service transitions, always invoked by the class manager.
class EbLevelLifecycle {
public:
    static void onLevelInit(EbAssistController& controller, PlayLayer* playLayer);
    static void resetAttempt(EbAssistController& controller, PlayLayer* playLayer);
    static void onCheckpointLoad(EbAssistController& controller, PlayLayer* playLayer);
    static void onLevelComplete(EbAssistController& controller, PlayLayer* playLayer);
    static void onLevelQuit(EbAssistController& controller, PlayLayer* playLayer);

private:
    EbLevelLifecycle() = delete;
};

#endif
