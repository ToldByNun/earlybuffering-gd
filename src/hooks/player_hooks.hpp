#ifndef PLAYER_HOOKS_HPP
#define PLAYER_HOOKS_HPP

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <fmt/format.h>

class $modify(EbPlayerObject, PlayerObject) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::pushButton", geode::prelude::Priority::First);
        (void)self.setHookPriority("PlayerObject::update", geode::prelude::Priority::First);
        // Swallow before death-FX / hitbox-on-death menus that hook this.
        (void)self.setHookPriority("PlayerObject::playerDestroyed", -10000000);
    }

    bool pushButton(PlayerButton button);

    void update(float deltaTime);

    void incrementJumps();

    void ringJump(RingObject* ring, bool skipCheck);

    void playerDestroyed(bool noEffects);

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to);

    void playBumpEffect(int objectType, GameObject* object);

    void spawnCircle();

    void spawnDualCircle();

    void spawnPortalCircle(cocos2d::ccColor3B color, float startRadius);

    void spawnScaleCircle();

private:
};

class $modify(EbPlayerObjectLate, PlayerObject) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::update", geode::prelude::Priority::Last);
    }

    void update(float deltaTime);

private:
};

#endif
