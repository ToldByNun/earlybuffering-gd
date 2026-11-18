#ifndef INPUT_SOURCE_HOOKS_HPP
#define INPUT_SOURCE_HOOKS_HPP
#include <Geode/modify/CCKeyboardDispatcher.hpp>
class $modify(EbSourceKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
public:
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double timestamp);

private:
};
#endif
