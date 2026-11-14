#ifndef NATIVE_ACTION_HOOKS_HPP
#define NATIVE_ACTION_HOOKS_HPP

#include <Geode/modify/CCActionManager.hpp>

class $modify(EbNativeActionManager, cocos2d::CCActionManager) {
public:
    void addAction(cocos2d::CCAction* action, cocos2d::CCNode* target, bool paused);
    void removeAllActions();
    void removeAllActionsFromTarget(cocos2d::CCObject* target);
    void removeAction(cocos2d::CCAction* action);
    void removeActionByTag(unsigned int tag, cocos2d::CCObject* target);
    cocos2d::CCAction* getActionByTag(unsigned int tag, cocos2d::CCObject* target);
    unsigned int numberOfRunningActionsInTarget(cocos2d::CCObject* target);
    void pauseTarget(cocos2d::CCObject* target);
    void resumeTarget(cocos2d::CCObject* target);
    cocos2d::CCSet* pauseAllRunningActions();
    void resumeTargets(cocos2d::CCSet* targets);

private:
};

#endif
