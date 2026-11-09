#include "object_hooks.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "game_hook_simulation_guard.hpp"

auto EbEnhancedGameObject::activatedByPlayer(PlayerObject* player) -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        // Save pre-touch state for restore. Rings: physics via applyRingJump only —
        // calling the original here re-triggers orb VFX/particles on the real object.
        EbShadowSimulator::get().noteActivated(this);
        if (geode::prelude::typeinfo_cast<RingObject*>(this))
            return;
        EnhancedGameObject::activatedByPlayer(player);
        return;
    }
    EnhancedGameObject::activatedByPlayer(player);
}

auto EbEnhancedGameObject::powerOnObject(int state) -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        EbShadowSimulator::get().noteActivated(this);
        // Rings: skip — applyRingJump owns orb claim. Pads/other: physics only.
        if (geode::prelude::typeinfo_cast<RingObject*>(this))
            return;
        EnhancedGameObject::powerOnObject(state);
        return;
    }
    EnhancedGameObject::powerOnObject(state);
}

auto EbEnhancedGameObject::powerOffObject() -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        EnhancedGameObject::powerOffObject();
        return;
    }
    EnhancedGameObject::powerOffObject();
}

auto EbEffectGameObject::triggerObject(GJBaseGameLayer* layer, int uniqueID,
                                       gd::vector<int> const* remapKeys) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
}

auto EbGameObject::playShineEffect() -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    GameObject::playShineEffect();
}

auto EbGameObject::activateObject() -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        // Visual / "used" state on the real level object — skip. Physics already applied.
        EbShadowSimulator::get().noteActivated(this);
        return;
    }
    GameObject::activateObject();
}

auto EbGameObject::deactivateObject(bool deactivate) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    GameObject::deactivateObject(deactivate);
}

auto EbRingObject::spawnCircle() -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    RingObject::spawnCircle();
}

auto EbRingObject::powerOnObject(int state) -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        EbShadowSimulator::get().noteActivated(this);
        return;
    }
    RingObject::powerOnObject(state);
}

auto EbRingObject::triggerActivated(float xPosition) -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        EbShadowSimulator::get().noteActivated(this);
        return;
    }
    RingObject::triggerActivated(xPosition);
}

auto EbHardStreak::addPoint(cocos2d::CCPoint point) -> void {
    // Wave collision uses the trail. The shadow must be allowed to grow its own;
    // the real player's trail must not be touched during sim.
    if (EbGameHookSimulationGuard::simulating() && !EbShadowSimulator::get().isShadowTrail(this))
        return;
    HardStreak::addPoint(point);
}
