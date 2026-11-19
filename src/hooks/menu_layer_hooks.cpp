#include "menu_layer_hooks.hpp"
#include "../core/real_assist/real_assist_runner.hpp"

bool EbMenuLayer::init() {
    if (!MenuLayer::init())
        return false;
    EbRealAssistRunner::onMenuLayerReady();
    return true;
}
