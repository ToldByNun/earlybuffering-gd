#ifndef ASSIST_BUFFER_RUNTIME_HPP
#define ASSIST_BUFFER_RUNTIME_HPP

#include "../assist_controller.hpp"

class AssistBufferRuntime {
public:
    static void inject(AssistController& controller, GJBaseGameLayer* layer, bool down);
    static void flushPendingRelease(AssistController& controller, PlayLayer* playLayer);
    static void fireBuffered(AssistController& controller, PlayLayer* playLayer,
                             PlayerObject* player, bool allowRecheck);

private:
    AssistBufferRuntime() = delete;
};

#endif
