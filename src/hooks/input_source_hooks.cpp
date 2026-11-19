#include "input_source_hooks.hpp"
#include "../core/assist/input/input_source_scope.hpp"
bool EbSourceKeyboardDispatcher::dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down,
                                                     bool repeat, double timestamp) {
    EbInputReconciler::Source source;
    source.device = EbInputReconciler::Device::Keyboard;
    source.id = static_cast<int>(key);
    EbInputSourceScope scope(source);
    return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
}
