#ifndef COCOS_MEMORY_HOOKS_HPP
#define COCOS_MEMORY_HOOKS_HPP

#include <Geode/modify/CCAutoreleasePool.hpp>
#include <Geode/modify/CCObject.hpp>
#include <Geode/modify/CCPoolManager.hpp>

class $modify(EbNativePoolManager, cocos2d::CCPoolManager) {
public:
    void addObject(cocos2d::CCObject* object);
    void removeObject(cocos2d::CCObject* object);
    void push();
    void pop();

private:
};

class $modify(EbNativeAutoreleasePool, cocos2d::CCAutoreleasePool) {
public:
    void addObject(cocos2d::CCObject* object);
    void removeObject(cocos2d::CCObject* object);
    void clear();

private:
};

class $modify(EbNativeReferenceCount, cocos2d::CCObject) {
public:
    void retain();
    void release();
    cocos2d::CCObject* autorelease();

private:
};

#endif
