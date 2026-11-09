#include "player_hooks.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "game_hook_simulation_guard.hpp"

auto EbPlayerObject::pushButton(PlayerButton button) -> bool {
    if (!EbGameHookSimulationGuard::simulating() &&
        !EbAssistController::get().allowPushButton(this, button)) {
        return false;
    }
    return PlayerObject::pushButton(button);
}

auto EbPlayerObject::update(float deltaTime) -> void {
    // Owned ghosts only step from ShadowSimulator::stepPhysics while simulating.
    if (EbShadowSimulator::get().isOwnedGhost(this)) {
        if (!EbGameHookSimulationGuard::simulating())
            return;
        PlayerObject::update(deltaTime);
        return;
    }
    if (!EbGameHookSimulationGuard::simulating()) {
        if (auto* playLayer = PlayLayer::get(); playLayer && this == playLayer->m_player1) {
            auto& ac = EbAssistController::get();
            ac.onPhysicsTick(playLayer, deltaTime);
            // begin/endRealUpdate brackets mid-step / CBF clicks for effectTick.
            ac.beginRealUpdate();
            PlayerObject::update(deltaTime);
            ac.endRealUpdate();
            return;
        }
    }
    PlayerObject::update(deltaTime);
}

auto EbPlayerObject::incrementJumps() -> void {
    if (EbGameHookSimulationGuard::simulating()) {
        EbShadowSimulator::get().noteJump();
        // Still run the original so orb/pad jump bookkeeping on the shadow stays consistent.
        PlayerObject::incrementJumps();
        return;
    }
    EbAssistController::get().noteRealJump(this);
    PlayerObject::incrementJumps();
}

auto EbPlayerObject::ringJump(RingObject* ring, bool skipCheck) -> void {
    if (EbShadowSimulator::get().isOwnedGhost(this) && !EbGameHookSimulationGuard::simulating())
        return;
    if (EbGameHookSimulationGuard::simulating()) {
        auto& sim = EbShadowSimulator::get();
        // applyRingJump → ringJump: passthrough to the real implementation once.
        if (sim.isApplyingRing()) {
            PlayerObject::ringJump(ring, skipCheck);
            return;
        }
        sim.applyRingJump(this, ring);
        return;
    }
    PlayerObject::ringJump(ring, skipCheck);
}

auto EbPlayerObject::playerDestroyed(bool noEffects) -> void {
    // Ghosts must never run the vanilla destroyed path (death FX / fail state).
    if (EbShadowSimulator::get().isOwnedGhost(this))
        return;
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::playerDestroyed(noEffects);
}

auto EbPlayerObject::playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::playSpiderDashEffect(from, to);
}

auto EbPlayerObject::playBumpEffect(int objectType, GameObject* object) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::playBumpEffect(objectType, object);
}

auto EbPlayerObject::spawnCircle() -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::spawnCircle();
}

auto EbPlayerObject::spawnDualCircle() -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::spawnDualCircle();
}

auto EbPlayerObject::spawnPortalCircle(cocos2d::ccColor3B color, float startRadius) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::spawnPortalCircle(color, startRadius);
}

auto EbPlayerObject::spawnScaleCircle() -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayerObject::spawnScaleCircle();
}

auto EbPlayerObjectLate::update(float deltaTime) -> void {
    if (EbShadowSimulator::get().isOwnedGhost(this)) {
        if (!EbGameHookSimulationGuard::simulating())
            return;
        PlayerObject::update(deltaTime);
        return;
    }
    if (!EbGameHookSimulationGuard::simulating())
        EbAssistController::get().onSubStep(this, deltaTime);
    PlayerObject::update(deltaTime);
}
