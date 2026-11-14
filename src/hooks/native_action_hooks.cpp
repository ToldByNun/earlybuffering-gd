#include "native_action_hooks.hpp"
#include "../core/sim/runtime/native_action_context.hpp"

void EbNativeActionManager::addAction(cocos2d::CCAction* action, cocos2d::CCNode* target,
                                      bool paused) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->addAction(action, target, paused);
        return;
    }
    cocos2d::CCActionManager::addAction(action, target, paused);
}

void EbNativeActionManager::removeAllActions() {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->removeAllActions();
        return;
    }
    cocos2d::CCActionManager::removeAllActions();
}

void EbNativeActionManager::removeAllActionsFromTarget(cocos2d::CCObject* target) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->removeAllActionsFromTarget(target);
        return;
    }
    cocos2d::CCActionManager::removeAllActionsFromTarget(target);
}

void EbNativeActionManager::removeAction(cocos2d::CCAction* action) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->removeAction(action);
        return;
    }
    cocos2d::CCActionManager::removeAction(action);
}

void EbNativeActionManager::removeActionByTag(unsigned int tag, cocos2d::CCObject* target) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->removeActionByTag(tag, target);
        return;
    }
    cocos2d::CCActionManager::removeActionByTag(tag, target);
}

cocos2d::CCAction* EbNativeActionManager::getActionByTag(unsigned int tag,
                                                         cocos2d::CCObject* target) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        return isolated->getActionByTag(tag, target);
    }
    return cocos2d::CCActionManager::getActionByTag(tag, target);
}

unsigned int EbNativeActionManager::numberOfRunningActionsInTarget(cocos2d::CCObject* target) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        return isolated->numberOfRunningActionsInTarget(target);
    }
    return cocos2d::CCActionManager::numberOfRunningActionsInTarget(target);
}

void EbNativeActionManager::pauseTarget(cocos2d::CCObject* target) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->pauseTarget(target);
        return;
    }
    cocos2d::CCActionManager::pauseTarget(target);
}

void EbNativeActionManager::resumeTarget(cocos2d::CCObject* target) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->resumeTarget(target);
        return;
    }
    cocos2d::CCActionManager::resumeTarget(target);
}

cocos2d::CCSet* EbNativeActionManager::pauseAllRunningActions() {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        return isolated->pauseAllRunningActions();
    }
    return cocos2d::CCActionManager::pauseAllRunningActions();
}

void EbNativeActionManager::resumeTargets(cocos2d::CCSet* targets) {
    if (auto* isolated = EbNativeActionContext::redirect(this)) {
        isolated->resumeTargets(targets);
        return;
    }
    cocos2d::CCActionManager::resumeTargets(targets);
}
