#ifndef ASSIST_OVERLAY_HPP
#define ASSIST_OVERLAY_HPP

#include "../assist_controller.hpp"

class AssistOverlay {
public:
    static void updateOverlay(AssistController& controller);

private:
    AssistOverlay() = delete;
};

#endif
