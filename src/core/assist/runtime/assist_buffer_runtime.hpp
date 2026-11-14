#ifndef ASSIST_BUFFER_RUNTIME_HPP
#define ASSIST_BUFFER_RUNTIME_HPP

#include "../assist_controller.hpp"

class EbAssistBufferRuntime {
public:
    static float substepPrefix(EbAssistController& controller, PlayerObject* player,
                               float deltaTime);
    static void fireSubstep(EbAssistController& controller, PlayerObject* player);
    static void inject(EbAssistController& controller, GJBaseGameLayer* layer, bool down);
    static void flushPendingRelease(EbAssistController& controller, PlayLayer* playLayer);
    static void fireBuffered(EbAssistController& controller, PlayLayer* playLayer,
                             PlayerObject* player, bool allowRecheck);

private:
    EbAssistBufferRuntime() = delete;
};

#endif
