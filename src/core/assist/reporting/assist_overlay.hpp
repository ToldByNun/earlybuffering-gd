#ifndef ASSIST_OVERLAY_HPP
#define ASSIST_OVERLAY_HPP

#include "../assist_controller.hpp"

class EbAssistOverlay {
public:
    static void updateOverlay(EbAssistController& controller);

private:
    EbAssistOverlay() = delete;
};

#endif
