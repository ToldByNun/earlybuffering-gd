#ifndef SHADOW_PHYSICS_HPP
#define SHADOW_PHYSICS_HPP

#include "../shadow_simulator.hpp"

class EbShadowPhysics {
public:
    static void markDead(EbShadowSimulator& simulator, PlayerObject* player, GameObject* object);
    static void noteJump(EbShadowSimulator& simulator);
    static void noteActivated(EbShadowSimulator& simulator, GameObject* gobj);
    static void noteOrbOverlap(EbShadowSimulator& simulator);
    static void applyRingJump(EbShadowSimulator& simulator, PlayerObject* shadow, RingObject* ring);
    static void stepPreCollide(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* state,
                               float deltaTime);
    static void stepPhysics(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* state,
                            float stepDt, float deltaTime, bool splitPrefix = false,
                            cocos2d::CCPoint const* tickStart = nullptr);

private:
    EbShadowPhysics() = delete;
};

#endif
