#ifndef OBJECT_HOOKS_HPP
#define OBJECT_HOOKS_HPP

#include <Geode/Geode.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/RingObject.hpp>
#include <fmt/format.h>

class $modify(EbEnhancedGameObject, EnhancedGameObject) {
public:
    void activatedByPlayer(PlayerObject* player);

    void powerOnObject(int state);

    void powerOffObject();

private:
};

class $modify(EbEffectGameObject, EffectGameObject) {
public:
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys);

private:
};

class $modify(EbGameObject, GameObject) {
public:
    void playShineEffect();

    void activateObject();

    void deactivateObject(bool deactivate);

private:
};

class $modify(EbRingObject, RingObject) {
public:
    void spawnCircle();

    void powerOnObject(int state);

    void triggerActivated(float xPosition);

private:
};

class $modify(EbHardStreak, HardStreak) {
public:
    void addPoint(cocos2d::CCPoint point);

private:
};

#endif
