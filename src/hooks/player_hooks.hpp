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
        (void)self.setHookPriorityPre("PlayerObject::playerDestroyed",
                                      geode::prelude::Priority::Stub);
        (void)self.setHookPriorityPre("PlayerObject::collidedWithObjectInternal",
                                      geode::prelude::Priority::Stub);
    }

    bool pushButton(PlayerButton button);

    void update(float deltaTime);

    void incrementJumps();

    void ringJump(RingObject* ring, bool skipCheck);

    bool collidedWithObjectInternal(float deltaTime, GameObject* object, cocos2d::CCRect rect,
                                    bool skipCheck);

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
