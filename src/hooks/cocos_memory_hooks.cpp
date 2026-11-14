#include "cocos_memory_hooks.hpp"
#include "../core/sim/runtime/native_memory_guard.hpp"

void EbNativePoolManager::addObject(cocos2d::CCObject* object) {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCPoolManager::addObject(object);
}

void EbNativePoolManager::removeObject(cocos2d::CCObject* object) {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCPoolManager::removeObject(object);
}

void EbNativePoolManager::push() {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCPoolManager::push();
}

void EbNativePoolManager::pop() {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCPoolManager::pop();
}

void EbNativeAutoreleasePool::addObject(cocos2d::CCObject* object) {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCAutoreleasePool::addObject(object);
}

void EbNativeAutoreleasePool::removeObject(cocos2d::CCObject* object) {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCAutoreleasePool::removeObject(object);
}

void EbNativeAutoreleasePool::clear() {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCAutoreleasePool::clear();
}

void EbNativeReferenceCount::retain() {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCObject::retain();
}

void EbNativeReferenceCount::release() {
    EbNativeMemoryGuard::Lock const lock;
    cocos2d::CCObject::release();
}

cocos2d::CCObject* EbNativeReferenceCount::autorelease() {
    EbNativeMemoryGuard::Lock const lock;
    EbNativeMemoryGuard::noteAutorelease();
    return cocos2d::CCObject::autorelease();
}
