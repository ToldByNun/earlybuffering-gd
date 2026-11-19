#ifndef SECTION_OBJECT_WALK_HPP
#define SECTION_OBJECT_WALK_HPP

#include <Geode/cocos/cocoa/CCArray.h>
#include <cstdint>
#include <functional>
#include <string>

class GameObject;
class GJBaseGameLayer;

// Walk the level-owned object array. Collision sections are non-owning and may
// contain stale slots; address plausibility cannot establish object lifetime.
class EbSectionObjectWalk {
public:
    // Reject null, null-page, and classic poison (~0) without dereferencing.
    static bool pointerLooksLive(void const* valuePointer);

    // Caller must supply an object held by a live owning Cocos collection.
    static GameObject* asGameObject(cocos2d::CCObject* object);

    static cocos2d::CCArray* objectArray(GJBaseGameLayer* layer);

    // Visit the owning list, falling back to object-layer children if unavailable.
    // Callbacks must not remove objects from the owning collection.
    static int forEachOwnedObject(GJBaseGameLayer* layer,
                                  std::function<void(GameObject*)> const& visit);

    static bool selfCheck(std::string& report);

private:
    EbSectionObjectWalk() = delete;
};

#endif
