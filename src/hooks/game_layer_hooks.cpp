#include "game_layer_hooks.hpp"
#include "../config/stage_config.hpp"
#include "../core/assist_controller.hpp"
#include "../core/bake_scheduler.hpp"
#include "../core/orb_stack_resolver.hpp"
#include "../core/shadow_simulator.hpp"
#include "../core/sim_collision_policy.hpp"
#include "../core/toggle_orb_sim.hpp"
#include "../core/trigger_classify.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../ui/status_overlay.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>
#include <fmt/format.h>

inline bool GameLayerHooksImplementation::simulating() {
    return ShadowSimulator::get().isSimulating();
}

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
    if (GameLayerHooksImplementation::simulating())
        return; // never let anything poke the real input path mid-sim
    if (AssistController::get().onHandleButton(this, down, button, isPlayer1)) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
}

auto EbGJBaseGameLayer::flipGravity(PlayerObject* player, bool flip, bool noEffects) -> void {
    if (GameLayerHooksImplementation::simulating()) {
        // only touch the shadow, never camera / effects / game state
        if (player)
            player->flipGravity(flip, true);
        return;
    }
    GJBaseGameLayer::flipGravity(player, flip, noEffects);
}

auto EbGJBaseGameLayer::gameEventTriggered(GJGameEvent event, int material, int playerID) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    GJBaseGameLayer::gameEventTriggered(event, material, playerID);
}

auto EbGJBaseGameLayer::canBeActivatedByPlayer(PlayerObject* player,
                                               EffectGameObject* object) -> bool {
    if (GameLayerHooksImplementation::simulating()) {
        // Only the shadow may "touch" orbs/pads. Real player must not be mutated mid-sim.
        if (!ShadowSimulator::get().isShadow(player))
            return false;
    }
    return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
}

auto EbGJBaseGameLayer::playerTouchedRing(PlayerObject* player, RingObject* ring) -> void {
    if (GameLayerHooksImplementation::simulating()) {
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

auto EbGJBaseGameLayer::playerTouchedTrigger(PlayerObject* player,
                                             EffectGameObject* object) -> void {
    if (GameLayerHooksImplementation::simulating()) {
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
    if (GameLayerHooksImplementation::simulating()) {
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
    if (!(GameLayerHooksImplementation::simulating())) {
        return GJBaseGameLayer::spawnParticle(plist, zOrder, positionType, position);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::createParticle(int objectType, char const* plistName, int tag,
                                       cocos2d::tCCPositionType positionType)
    -> cocos2d::CCParticleSystemQuad* {
    if (!(GameLayerHooksImplementation::simulating())) {
        return GJBaseGameLayer::createParticle(objectType, plistName, tag, positionType);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::claimParticle(gd::string key,
                                      int zLayer) -> cocos2d::CCParticleSystemQuad* {
    if (!(GameLayerHooksImplementation::simulating())) {
        return GJBaseGameLayer::claimParticle(key, zLayer);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::createCustomParticle(gd::string const& key,
                                             cocos2d::ParticleStruct const& particleStruct,
                                             int minimum,
                                             bool dontAdd) -> cocos2d::CCParticleSystemQuad* {
    if (!(GameLayerHooksImplementation::simulating())) {
        return GJBaseGameLayer::createCustomParticle(key, particleStruct, minimum, dontAdd);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::claimCustomParticle(gd::string const& key,
                                            cocos2d::ParticleStruct const& particleStruct,
                                            int zLayer, int zOrder, int uiObject,
                                            bool dontAdd) -> cocos2d::CCParticleSystemQuad* {
    if (!(GameLayerHooksImplementation::simulating())) {
        return GJBaseGameLayer::claimCustomParticle(key, particleStruct, zLayer, zOrder, uiObject,
                                                    dontAdd);
    }
    return nullptr;
}

auto EbGJBaseGameLayer::spawnParticleTrigger(int particleID, cocos2d::CCPoint position,
                                             float rotation, float scale) -> void {
    if (GameLayerHooksImplementation::simulating())
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

auto EbPlayerObject::pushButton(PlayerButton button) -> bool {
    if (!GameLayerHooksImplementation::simulating() &&
        !AssistController::get().allowPushButton(this, button)) {
        return false;
    }
    return PlayerObject::pushButton(button);
}

auto EbPlayerObject::update(float deltaTime) -> void {
    // Owned ghosts only step from ShadowSimulator::stepPhysics while simulating.
    if (ShadowSimulator::get().isOwnedGhost(this)) {
        if (!GameLayerHooksImplementation::simulating())
            return;
        PlayerObject::update(deltaTime);
        return;
    }
    if (!GameLayerHooksImplementation::simulating()) {
        if (auto* playLayer = PlayLayer::get(); playLayer && this == playLayer->m_player1) {
            auto& ac = AssistController::get();
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
    if (GameLayerHooksImplementation::simulating()) {
        ShadowSimulator::get().noteJump();
        // Still run the original so orb/pad jump bookkeeping on the shadow stays consistent.
        PlayerObject::incrementJumps();
        return;
    }
    AssistController::get().noteRealJump(this);
    PlayerObject::incrementJumps();
}

auto EbPlayerObject::ringJump(RingObject* ring, bool skipCheck) -> void {
    if (ShadowSimulator::get().isOwnedGhost(this) && !GameLayerHooksImplementation::simulating())
        return;
    if (GameLayerHooksImplementation::simulating()) {
        auto& sim = ShadowSimulator::get();
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
    if (ShadowSimulator::get().isOwnedGhost(this))
        return;
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::playerDestroyed(noEffects);
}

auto EbPlayerObject::playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::playSpiderDashEffect(from, to);
}

auto EbPlayerObject::playBumpEffect(int objectType, GameObject* object) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::playBumpEffect(objectType, object);
}

auto EbPlayerObject::spawnCircle() -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::spawnCircle();
}

auto EbPlayerObject::spawnDualCircle() -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::spawnDualCircle();
}

auto EbPlayerObject::spawnPortalCircle(cocos2d::ccColor3B color, float startRadius) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::spawnPortalCircle(color, startRadius);
}

auto EbPlayerObject::spawnScaleCircle() -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayerObject::spawnScaleCircle();
}

auto EbPlayerObjectLate::update(float deltaTime) -> void {
    if (ShadowSimulator::get().isOwnedGhost(this)) {
        if (!GameLayerHooksImplementation::simulating())
            return;
        PlayerObject::update(deltaTime);
        return;
    }
    if (!GameLayerHooksImplementation::simulating())
        AssistController::get().onSubStep(this, deltaTime);
    PlayerObject::update(deltaTime);
}

auto EbEnhancedGameObject::activatedByPlayer(PlayerObject* player) -> void {
    if (GameLayerHooksImplementation::simulating()) {
        // Save pre-touch state for restore. Rings: physics via applyRingJump only —
        // calling the original here re-triggers orb VFX/particles on the real object.
        ShadowSimulator::get().noteActivated(this);
        if (geode::prelude::typeinfo_cast<RingObject*>(this))
            return;
        EnhancedGameObject::activatedByPlayer(player);
        return;
    }
    EnhancedGameObject::activatedByPlayer(player);
}

auto EbEnhancedGameObject::powerOnObject(int state) -> void {
    if (GameLayerHooksImplementation::simulating()) {
        ShadowSimulator::get().noteActivated(this);
        // Rings: skip — applyRingJump owns orb claim. Pads/other: physics only.
        if (geode::prelude::typeinfo_cast<RingObject*>(this))
            return;
        EnhancedGameObject::powerOnObject(state);
        return;
    }
    EnhancedGameObject::powerOnObject(state);
}

auto EbEnhancedGameObject::powerOffObject() -> void {
    if (GameLayerHooksImplementation::simulating()) {
        EnhancedGameObject::powerOffObject();
        return;
    }
    EnhancedGameObject::powerOffObject();
}

auto EbEffectGameObject::triggerObject(GJBaseGameLayer* layer, int uniqueID,
                                       gd::vector<int> const* remapKeys) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
}

auto EbGameObject::playShineEffect() -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    GameObject::playShineEffect();
}

auto EbGameObject::activateObject() -> void {
    if (GameLayerHooksImplementation::simulating()) {
        // Visual / "used" state on the real level object — skip. Physics already applied.
        ShadowSimulator::get().noteActivated(this);
        return;
    }
    GameObject::activateObject();
}

auto EbGameObject::deactivateObject(bool deactivate) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    GameObject::deactivateObject(deactivate);
}

auto EbRingObject::spawnCircle() -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    RingObject::spawnCircle();
}

auto EbRingObject::powerOnObject(int state) -> void {
    if (GameLayerHooksImplementation::simulating()) {
        ShadowSimulator::get().noteActivated(this);
        return;
    }
    RingObject::powerOnObject(state);
}

auto EbRingObject::triggerActivated(float xPosition) -> void {
    if (GameLayerHooksImplementation::simulating()) {
        ShadowSimulator::get().noteActivated(this);
        return;
    }
    RingObject::triggerActivated(xPosition);
}

auto EbHardStreak::addPoint(cocos2d::CCPoint point) -> void {
    // Wave collision uses the trail. The shadow must be allowed to grow its own;
    // the real player's trail must not be touched during sim.
    if (GameLayerHooksImplementation::simulating() && !ShadowSimulator::get().isShadowTrail(this))
        return;
    HardStreak::addPoint(point);
}

auto EbPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreatePlayer) -> bool {
    if (!PlayLayer::init(level, useReplay, dontCreatePlayer))
        return false;
    AssistController::get().onLevelInit(this);
    return true;
}

auto EbPlayLayer::resetLevel() -> void {
    // Ghosts land in section vectors via sim checkCollisions. Vanilla
    // resetLevel → sortSectionVector then AVs on those bodies — evacuate first.
    PerfTrace::Scope const scope("resetLevelHook");
    LagProbe::get().beginEpisode("resetLevel", fmt::format("attemptTickWas={} practice={}",
                                                           AssistController::get().attemptTick(),
                                                           this->m_isPracticeMode));
    {
        LagProbe::Scope const state("eb.bakeCancel");
        BakeScheduler::get().cancel();
    }
    ShadowSimulator::get().evacuateGhosts(this);
    AssistController::get().resetAttempt(this);
    {
        LagProbe::Scope const state("vanilla.PlayLayer::resetLevel");
        PlayLayer::resetLevel();
    }
    LagProbe::get().endEpisode();
}

auto EbPlayLayer::onQuit() -> void {
    AssistController::get().onLevelQuit();
    PlayLayer::onQuit();
}

auto EbPlayLayer::destroyPlayer(PlayerObject* player, GameObject* object) -> void {
    auto& sim = ShadowSimulator::get();

    // Ghosts (shadow + snapshots) must NEVER enter vanilla destroyPlayer.
    // That path can fail the real attempt — looks like "death to nothing"
    // because the death effect is on m_player1 while the collision was on
    // an invisible ghost left at a sim/hazard position.
    // Also covers anticheatSpike: only real players may take that path.
    if (sim.isOwnedGhost(player)) {
        if (sim.isSimulating()) {
            sim.markDead(player, object);
        }
        return;
    }

    // Real player colliding with an owned ghost-as-object (wave vs invisible
    // snapshot/shadow body) — treat as non-event, not a level fail.
    if (auto* killer = geode::prelude::typeinfo_cast<PlayerObject*>(object)) {
        if (sim.isOwnedGhost(killer)) {
            AssistLog::debug("destroyPlayer: swallowed death to owned ghost body");
            if (player)
                player->m_isDead = false;
            return;
        }
    }

    // While simulating, swallow real-player deaths too (sim must not bleed).
    // Anticheat on the real player still goes through.
    if (sim.isSimulating()) {
        if (object == m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        if (player)
            player->m_isDead = false;
        return;
    }

    if (object != m_anticheatSpike) {
        auto const mode = player ? StageConfig::kindName(StageConfig::kindOf(player)) : "?";
        LagProbe::get().beginEpisode(
            "death", fmt::format("x={:.1f} y={:.1f} tick={} mode={} killer={} practice={}",
                                 player ? player->getPositionX() : 0.f,
                                 player ? player->getPositionY() : 0.f,
                                 AssistController::get().attemptTick(), mode ? mode : "?",
                                 object ? object->m_objectID : -1, this->m_isPracticeMode));
        AssistController::get().onRealDeath(this, player, object);
        {
            LagProbe::Scope const state("vanilla.destroyPlayer");
            PlayLayer::destroyPlayer(player, object);
        }
        LagProbe::get().endEpisode();
        return;
    }
    PlayLayer::destroyPlayer(player, object);
}

auto EbPlayLayer::playEndAnimationToPos(cocos2d::CCPoint position) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayLayer::playEndAnimationToPos(position);
}

auto EbPlayLayer::playGravityEffect(bool flip) -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayLayer::playGravityEffect(flip);
}

auto EbPlayLayer::spawnCircle() -> void {
    if (GameLayerHooksImplementation::simulating())
        return;
    PlayLayer::spawnCircle();
}

auto EbPlayLayer::levelComplete() -> void {
    // Safe Mode OFF → never force test mode (progress like vanilla).
    // Safe Mode ON → enabled or assist-touched runs count as test clears.
    bool const block = StageConfig::blocksProgressSaves(AssistController::get().assistTouchedRun());
    if (block) {
        this->m_isTestMode = true;
        AssistLog::debug("Safe Mode ON: levelComplete → testMode (no normal progress)");
    } else {
        AssistLog::debug("Safe Mode OFF: levelComplete allows normal progress");
    }
    AssistController::get().onLevelComplete(this);
    PlayLayer::levelComplete();
}

auto EbPlayLayer::showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry,
                              bool noTitle) -> void {
    if (StageConfig::blocksProgressSaves(AssistController::get().assistTouchedRun())) {
        AssistLog::debug("Safe Mode ON: showNewBest swallowed");
        return;
    }
    PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
}

auto EbPlayLayer::loadFromCheckpoint(CheckpointObject* checkpoint) -> void {
    LagProbe::get().beginEpisode(
        "loadFromCheckpoint",
        fmt::format("attemptTickWas={}", AssistController::get().attemptTick()));
    {
        LagProbe::Scope const state("eb.bakeCancel");
        BakeScheduler::get().cancel();
    }
    ShadowSimulator::get().evacuateGhosts(this);
    {
        LagProbe::Scope const state("eb.onCheckpointLoad");
        AssistController::get().onCheckpointLoad();
    }
    {
        LagProbe::Scope const state("vanilla.loadFromCheckpoint");
        PlayLayer::loadFromCheckpoint(checkpoint);
    }
    LagProbe::get().endEpisode();
}

auto EbGJGameLevel::savePercentage(int percent, bool isPracticeMode, int clicks, int attempts,
                                   bool isChkValid) -> void {
    // Same contract as PlayLayer::showNewBest / levelComplete (live Safe Mode read).
    if (StageConfig::blocksProgressSaves(AssistController::get().assistTouchedRun())) {
        return;
    }
    GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
}
