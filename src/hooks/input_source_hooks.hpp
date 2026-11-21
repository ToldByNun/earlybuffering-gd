#ifndef INPUT_SOURCE_HOOKS_HPP
#define INPUT_SOURCE_HOOKS_HPP

// iOS bindings leave CCKeyboardDispatcher ctor/dtor addresses empty; $modify then
// expands GEODE_APPLY_MODIFY_FOR_CONSTRUCTOR(, ...) and fails the clang build.
// Keyboard jump tagging is desktop/mac/android-relevant; iOS is touch-primary.
#if !defined(GEODE_IS_IOS)

#include <Geode/modify/CCKeyboardDispatcher.hpp>

class $modify(EbSourceKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
public:
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double timestamp);

private:
};

#endif

#endif
