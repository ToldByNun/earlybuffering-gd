#include "game_layer_hooks.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/sim/orbs/orb_stack_resolver.hpp"
#include "../core/sim/orbs/toggle_orb_sim.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/sim/sim_collision_policy.hpp"
#include "../core/trigger_world/trigger_classify.hpp"
#include "game_hook_simulation_guard.hpp"
#include <Geode/Geode.hpp>
#include <fmt/format.h>

// ---------------------------------------------------------------------------
// Input + physics tick
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------

// Runs *after* every other hook on PlayerObject::update (notably Click Between Frames,
// which splits one step into several sub-updates). Lets us know how much of the current
// step has already been simulated when a mid-step input arrives.

// ---------------------------------------------------------------------------
// Objects touched by the shadow: no triggers, no visuals; flags restored after sim
// ---------------------------------------------------------------------------

// Orb / pad ripple + power visuals. Real level objects must not show sim touches.

// ---------------------------------------------------------------------------
// PlayLayer lifecycle + death detection
// ---------------------------------------------------------------------------

auto EbGJBaseGameLayer::handleButton(bool down, int button, bool isPlayer1) -> void {
    if (GameHookSimulationGuard::simulating())
        return; // never let anything poke the real input path mid-sim
    if (AssistController::get().onHandleButton(this, down, button, isPlayer1)) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
}

auto EbGJBaseGameLayer::flipGravity(PlayerObject* player, bool flip, bool noEffects) -> void {
    if (GameHookSimulationGuard::simulating()) {
        // only touch the shadow, never camera / effects / game state
        if (player)
            player->flipGravity(flip, true);
        return;
    }
    GJBaseGameLayer::flipGravity(player, flip, noEffects);
}

auto EbGJBaseGameLayer::gameEventTriggered(GJGameEvent event, int material, int playerID) -> void {
    if (GameHookSimulationGuard::simulating())
        return;
    GJBaseGameLayer::gameEventTriggered(event, material, playerID);
}

auto EbGJBaseGameLayer::canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object)
    -> bool {
    if (GameHookSimulationGuard::simulating()) {
        // Only the shadow may "touch" orbs/pads. Real player must not be mutated mid-sim.
        if (!ShadowSimulator::get().isShadow(player))
            return false;
    }
    return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
}

auto EbGJBaseGameLayer::playerTouchedRing(PlayerObject* player, RingObject* ring) -> void {
    if (GameHookSimulationGuard::simulating()) {
        auto& sim = ShadowSimulator::get();
        if (sim.isOrbScanning()) {
            sim.noteOrbOverlap();
            return;
        }
        if (ring && TriggerClassify::isToggleOrbId(ring->m_objectID)) {
            if (!sim.isShadow(player))
                return;
            auto* playLayer = geode::prelude::typeinfo_cast<PlayLayer*>(this);
            OrbStackResolver::get().noteTouch(player, ring, false, sim.simAbsTick());
            ToggleOrbSim::get().onToggleHit(playLayer, player, ring);
            // Stacked normal orb must also fire.
            OrbStackResolver::get().fireStackedPartners(playLayer, player, ring);
            return;
        }
        bool const beforeClaim = ring && (ring->m_claimTouch || ring->m_activated);
        sim.applyRingJump(player, ring);
        bool const afterClaim = ring && (ring->m_claimTouch || ring->m_activated);
        OrbStackResolver::get().noteTouch(player, ring, !beforeClaim && afterClaim,
                                          sim.simAbsTick());
        // Stacked toggle must also fire (whichever partner collision hit first).
        auto* playLayer = geode::prelude::typeinfo_cast<PlayLayer*>(this);
        OrbStackResolver::get().fireStackedPartners(playLayer, player, ring);
        return;
    }
    // Live: null-reject only — never invent a call. Half-restored / poisoned
    // rings after a nested sim were a SIGBUS source on iOS (Problem C).
    if (!player || !ring)
        return;
    GJBaseGameLayer::playerTouchedRing(player, ring);
}

auto EbGJBaseGameLayer::playerTouchedTrigger(PlayerObject* player, EffectGameObject* object)
    -> void {
    if (GameHookSimulationGuard::simulating()) {
        auto& sim = ShadowSimulator::get();
        if (!sim.isShadow(player) || !object)
            return;
        // Touch-toggle 1049: model group flip; other triggers stay blocked.
        if (TriggerClassify::isToggleTriggerId(object->m_objectID) && object->m_isTouchTriggered) {
            auto* playLayer = geode::prelude::typeinfo_cast<PlayLayer*>(this);
            ToggleOrbSim::get().onToggleHit(playLayer, player, object);
            // Stacked normal orb must also fire (same rule as 1594).
            OrbStackResolver::get().fireStackedPartners(playLayer, player, object);
        }
        return;
    }
    GJBaseGameLayer::playerTouchedTrigger(player, object);
}

auto EbGJBaseGameLayer::bumpPlayer(PlayerObject* player, EffectGameObject* object) -> void {
    if (GameHookSimulationGuard::simulating()) {
        if (!ShadowSimulator::get().isShadow(player) || !object)
            return;
        ShadowSimulator::get().noteActivated(object);
        GJBaseGameLayer::bumpPlayer(player, object);
        return;
    }
    GJBaseGameLayer::bumpPlayer(player, object);
}

auto EbGJBaseGameLayer::spawnParticle(char const* plist, int zOrder,
                                      cocos2d::tCCPositionType positionType,
                                      cocos2d::CCPoint position) -> cocos2d::CCParticleSystemQuad* {
    if (!(GameHookSimulationGuard::simulating())) {
        return GJBaseGameLayer::spawnParticle(plist, zOrder, positionType, position);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::createParticle(int objectType, char const* plistName, int tag,
                                       cocos2d::tCCPositionType positionType)
    -> cocos2d::CCParticleSystemQuad* {
    if (!(GameHookSimulationGuard::simulating())) {
        return GJBaseGameLayer::createParticle(objectType, plistName, tag, positionType);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::claimParticle(gd::string key, int zLayer)
    -> cocos2d::CCParticleSystemQuad* {
    if (!(GameHookSimulationGuard::simulating())) {
        return GJBaseGameLayer::claimParticle(key, zLayer);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::createCustomParticle(gd::string const& key,
                                             cocos2d::ParticleStruct const& particleStruct,
                                             int minimum, bool dontAdd)
    -> cocos2d::CCParticleSystemQuad* {
    if (!(GameHookSimulationGuard::simulating())) {
        return GJBaseGameLayer::createCustomParticle(key, particleStruct, minimum, dontAdd);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::claimCustomParticle(gd::string const& key,
                                            cocos2d::ParticleStruct const& particleStruct,
                                            int zLayer, int zOrder, int uiObject, bool dontAdd)
    -> cocos2d::CCParticleSystemQuad* {
    if (!(GameHookSimulationGuard::simulating())) {
        return GJBaseGameLayer::claimCustomParticle(key, particleStruct, zLayer, zOrder, uiObject,
                                                    dontAdd);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::spawnParticleTrigger(int particleID, cocos2d::CCPoint position,
                                             float rotation, float scale) -> void {
    if (GameHookSimulationGuard::simulating())
        return;
    GJBaseGameLayer::spawnParticleTrigger(particleID, position, rotation, scale);
}

auto EbGJBaseGameLayer::collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* vec,
                                              int objectsCount, float deltaTime) -> void {
    auto& sim = ShadowSimulator::get();
    if (!vec || objectsCount <= 0) {
        GJBaseGameLayer::collisionCheckObjects(player, vec, objectsCount, deltaTime);
        return;
    }

    // Sim must only collide the shadow — never the real player mid-plan.
    // Use persistent scratch (not a stack gd::vector): on iOS ARM64 a temporary
    // gd::vector ABI/lifetime mismatch into playerTouchedRing was a SIGBUS source.
    if (sim.isSimulating()) {
        if (!sim.isShadow(player))
            return;
        auto& extra = sim.collisionFilterScratch();
        extra.clear();
        if (extra.capacity() < static_cast<size_t>(objectsCount)) {
            extra.reserve(static_cast<size_t>(objectsCount));
        }
        for (int index = 0; index < objectsCount; index++) {
            GameObject* object = vec->at(index);
            if (!object)
                continue;
            // Never collide the shadow with real P1/P2 or other ghosts —
            // same spawn pose → instant frame-0 "death" during bake/plan.
            if (geode::prelude::typeinfo_cast<PlayerObject*>(object)) {
                continue;
            }
            switch (SimCollisionPolicy::classify(object)) {
            case SimObjectAction::Include:
                extra.push_back(object);
                break;
            case SimObjectAction::UnknownOnOverlap:
                if (player && object->getObjectRect().intersectsRect(player->getObjectRect())) {
                    sim.noteUnknown(object);
                    SimCollisionPolicy::noteLimitedSimOnce();
                }
                break;
            case SimObjectAction::Skip:
            default:
                break;
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &extra, static_cast<int>(extra.size()),
                                               deltaTime);
        return;
    }

    // Live: strip owned ghosts if they ever appear in the candidate list
    // (invisible PlayerObjects in objectLayer can otherwise kill wave/"to nothing").
    bool anyGhost = false;
    for (int index = 0; index < objectsCount; index++) {
        GameObject* object = vec->at(index);
        if (!object)
            continue;
        if (auto* asPl = geode::prelude::typeinfo_cast<PlayerObject*>(object)) {
            if (sim.isOwnedGhost(asPl)) {
                anyGhost = true;
                break;
            }
        }
    }
    if (anyGhost) {
        auto& extra = sim.collisionFilterScratch();
        extra.clear();
        if (extra.capacity() < static_cast<size_t>(objectsCount)) {
            extra.reserve(static_cast<size_t>(objectsCount));
        }
        for (int index = 0; index < objectsCount; index++) {
            GameObject* object = vec->at(index);
            if (!object)
                continue;
            if (auto* asPl = geode::prelude::typeinfo_cast<PlayerObject*>(object)) {
                if (sim.isOwnedGhost(asPl))
                    continue;
            }
            extra.push_back(object);
        }
        GJBaseGameLayer::collisionCheckObjects(player, &extra, static_cast<int>(extra.size()),
                                               deltaTime);
        return;
    }
    GJBaseGameLayer::collisionCheckObjects(player, vec, objectsCount, deltaTime);
}
