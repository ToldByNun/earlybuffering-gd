#ifndef SHADOW_PHYSICS_HPP
#define SHADOW_PHYSICS_HPP

#include "../shadow_simulator.hpp"

class ShadowPhysics {
public:
    static void markDead(ShadowSimulator& simulator, PlayerObject* player, GameObject* object);
    static void noteJump(ShadowSimulator& simulator);
    static void noteActivated(ShadowSimulator& simulator, GameObject* gobj);
    static void noteOrbOverlap(ShadowSimulator& simulator);
    static void applyRingJump(ShadowSimulator& simulator, PlayerObject* shadow, RingObject* ring);
    static void stepPreCollide(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* state,
                               float deltaTime);
    static void stepPhysics(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* state,
                            float stepDt, float deltaTime);

private:
    ShadowPhysics() = delete;
};

#endif
