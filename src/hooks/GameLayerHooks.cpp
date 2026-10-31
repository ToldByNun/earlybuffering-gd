#include "../core/AssistController.hpp"
#include "../core/BakeScheduler.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/SimCollisionPolicy.hpp"
#include "../core/ToggleOrbSim.hpp"
#include "../core/TriggerClassify.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/LagProbe.hpp"
#include "../debug/PerfTrace.hpp"
#include "../ui/StatusOverlay.hpp"
#include <fmt/format.h>

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/RingObject.hpp>

using namespace geode::prelude;
inline bool simulating() { return ShadowSimulator::get().isSimulating(); }


// ---------------------------------------------------------------------------
// Input + physics tick
// ---------------------------------------------------------------------------
class $modify(EbGJBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", Priority::First);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (simulating()) return; // never let anything poke the real input path mid-sim
        if (AssistController::get().onHandleButton(this, down, button, isPlayer1)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    // --- simulation guards ---
    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (simulating()) {
            // only touch the shadow, never camera / effects / game state
            if (player) player->flipGravity(flip, true);
            return;
        }
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (simulating()) return;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (simulating()) {
            // Only the shadow may "touch" orbs/pads. Real player must not be mutated mid-sim.
            if (!ShadowSimulator::get().isShadow(player)) return false;
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    // Orbs: one physics jump on the shadow, claim for this run only, no VFX.
    // Requires Jump held (applyRingJump) — wait-ticks must not claim early.
    // Orb-window scan records overlap only (no boost / claim).
    // Toggle orb 1594: reversible group flip via ToggleOrbSim (no triggerObject).
    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (simulating()) {
            auto& sim = ShadowSimulator::get();
            if (sim.isOrbScanning()) {
                sim.noteOrbOverlap();
                return;
            }
            if (ring && TriggerClassify::isToggleOrbId(ring->m_objectID)) {
                if (!sim.isShadow(player)) return;
                auto* playLayer = typeinfo_cast<PlayLayer*>(this);
                ToggleOrbSim::get().onToggleHit(playLayer, player, ring);
                return;
            }
            sim.applyRingJump(player, ring);
            return;
        }
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (simulating()) {
            auto& sim = ShadowSimulator::get();
            if (!sim.isShadow(player) || !object) return;
            // Touch-toggle 1049: model group flip; other triggers stay blocked.
            if (TriggerClassify::isToggleTriggerId(object->m_objectID)
                && object->m_isTouchTriggered) {
                auto* playLayer = typeinfo_cast<PlayLayer*>(this);
                ToggleOrbSim::get().onToggleHit(playLayer, player, object);
            }
            return;
        }
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }

    // Pads: allow the boost for the shadow; VFX hooks below stay blocked.
    void bumpPlayer(PlayerObject* player, EffectGameObject* object) {
        if (simulating()) {
            if (!ShadowSimulator::get().isShadow(player) || !object) return;
            ShadowSimulator::get().noteActivated(object);
            GJBaseGameLayer::bumpPlayer(player, object);
            return;
        }
        GJBaseGameLayer::bumpPlayer(player, object);
    }

    cocos2d::CCParticleSystemQuad* spawnParticle(
        char const* plist, int zOrder, cocos2d::tCCPositionType positionType, cocos2d::CCPoint position
    ) {
        if (simulating()) return nullptr;
        return GJBaseGameLayer::spawnParticle(plist, zOrder, positionType, position);
    }

    cocos2d::CCParticleSystemQuad* createParticle(
        int objectType, char const* plistName, int tag, cocos2d::tCCPositionType positionType
    ) {
        if (simulating()) return nullptr;
        return GJBaseGameLayer::createParticle(objectType, plistName, tag, positionType);
    }

    cocos2d::CCParticleSystemQuad* claimParticle(gd::string key, int zLayer) {
        if (simulating()) return nullptr;
        return GJBaseGameLayer::claimParticle(key, zLayer);
    }

    cocos2d::CCParticleSystemQuad* createCustomParticle(
        gd::string const& key, cocos2d::ParticleStruct const& particleStruct, int minimum, bool dontAdd
    ) {
        if (simulating()) return nullptr;
        return GJBaseGameLayer::createCustomParticle(key, particleStruct, minimum, dontAdd);
    }

    cocos2d::CCParticleSystemQuad* claimCustomParticle(
        gd::string const& key, cocos2d::ParticleStruct const& particleStruct,
        int zLayer, int zOrder, int uiObject, bool dontAdd
    ) {
        if (simulating()) return nullptr;
        return GJBaseGameLayer::claimCustomParticle(key, particleStruct, zLayer, zOrder, uiObject, dontAdd);
    }

    void spawnParticleTrigger(int particleID, cocos2d::CCPoint position, float rotation, float scale) {
        if (simulating()) return;
        GJBaseGameLayer::spawnParticleTrigger(particleID, position, rotation, scale);
    }

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* vec, int objectsCount, float dt) {
        auto& sim = ShadowSimulator::get();

        // Sim must only collide the shadow — never the real player mid-plan.
        if (sim.isSimulating()) {
            if (!sim.isShadow(player)) return;
            gd::vector<GameObject*> extra;
            extra.reserve(objectsCount);
            for (int i = 0; i < objectsCount; i++) {
                GameObject* obj = vec->at(i);
                // Never collide the shadow with real P1/P2 or other ghosts —
                // same spawn pose → instant frame-0 "death" during bake/plan.
                if (typeinfo_cast<PlayerObject*>(obj)) {
                    continue;
                }
                switch (SimCollisionPolicy::classify(obj)) {
                    case SimObjectAction::Include:
                        extra.push_back(obj);
                        break;
                    case SimObjectAction::UnknownOnOverlap:
                        if (player && obj->getObjectRect().intersectsRect(player->getObjectRect())) {
                            sim.noteUnknown(obj);
                            SimCollisionPolicy::noteLimitedSimOnce();
                        }
                        break;
                    case SimObjectAction::Skip:
                    default:
                        break;
                }
            }
            GJBaseGameLayer::collisionCheckObjects(player, &extra, static_cast<int>(extra.size()), dt);
            return;
        }

        // Live: strip owned ghosts if they ever appear in the candidate list
        // (invisible PlayerObjects in objectLayer can otherwise kill wave/"to nothing").
        if (vec && objectsCount > 0) {
            bool anyGhost = false;
            for (int i = 0; i < objectsCount; i++) {
                if (auto* asPl = typeinfo_cast<PlayerObject*>(vec->at(i))) {
                    if (sim.isOwnedGhost(asPl)) { anyGhost = true; break; }
                }
            }
            if (anyGhost) {
                gd::vector<GameObject*> extra;
                extra.reserve(static_cast<size_t>(objectsCount));
                for (int i = 0; i < objectsCount; i++) {
                    GameObject* obj = vec->at(i);
                    if (auto* asPl = typeinfo_cast<PlayerObject*>(obj)) {
                        if (sim.isOwnedGhost(asPl)) continue;
                    }
                    extra.push_back(obj);
                }
                GJBaseGameLayer::collisionCheckObjects(
                    player, &extra, static_cast<int>(extra.size()), dt
                );
                return;
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, vec, objectsCount, dt);
    }
};

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
class $modify(EbPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::pushButton", Priority::First);
        (void)self.setHookPriority("PlayerObject::update", Priority::First);
    }

    bool pushButton(PlayerButton button) {
        if (!simulating() && !AssistController::get().allowPushButton(this, button)) {
            return false;
        }
        return PlayerObject::pushButton(button);
    }

    void update(float dt) {
        // Owned ghosts only step from ShadowSimulator::stepPhysics while simulating.
        if (ShadowSimulator::get().isOwnedGhost(this)) {
            if (!simulating()) return;
            PlayerObject::update(dt);
            return;
        }
        if (!simulating()) {
            if (auto* pl = PlayLayer::get(); pl && this == pl->m_player1) {
                auto& ac = AssistController::get();
                ac.onPhysicsTick(pl, dt);
                // begin/endRealUpdate brackets mid-step / CBF clicks for effectTick.
                ac.beginRealUpdate();
                PlayerObject::update(dt);
                ac.endRealUpdate();
                return;
            }
        }
        PlayerObject::update(dt);
    }

    void incrementJumps() {
        if (simulating()) {
            ShadowSimulator::get().noteJump();
            // Still run the original so orb/pad jump bookkeeping on the shadow stays consistent.
            PlayerObject::incrementJumps();
            return;
        }
        AssistController::get().noteRealJump(this);
        PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (ShadowSimulator::get().isOwnedGhost(this) && !simulating()) return;
        if (simulating()) {
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

    void playerDestroyed(bool noEffects) {
        // Ghosts must never run the vanilla destroyed path (death FX / fail state).
        if (ShadowSimulator::get().isOwnedGhost(this)) return;
        if (simulating()) return;
        PlayerObject::playerDestroyed(noEffects);
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (simulating()) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void playBumpEffect(int objectType, GameObject* object) {
        if (simulating()) return;
        PlayerObject::playBumpEffect(objectType, object);
    }

    void spawnCircle() {
        if (simulating()) return;
        PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        if (simulating()) return;
        PlayerObject::spawnDualCircle();
    }

    void spawnPortalCircle(cocos2d::ccColor3B color, float startRadius) {
        if (simulating()) return;
        PlayerObject::spawnPortalCircle(color, startRadius);
    }

    void spawnScaleCircle() {
        if (simulating()) return;
        PlayerObject::spawnScaleCircle();
    }
};

// Runs *after* every other hook on PlayerObject::update (notably Click Between Frames,
// which splits one step into several sub-updates). Lets us know how much of the current
// step has already been simulated when a mid-step input arrives.
class $modify(EbPlayerObjectLate, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::update", Priority::Last);
    }

    void update(float dt) {
        if (ShadowSimulator::get().isOwnedGhost(this)) {
            if (!simulating()) return;
            PlayerObject::update(dt);
            return;
        }
        if (!simulating()) AssistController::get().onSubStep(this, dt);
        PlayerObject::update(dt);
    }
};

// ---------------------------------------------------------------------------
// Objects touched by the shadow: no triggers, no visuals; flags restored after sim
// ---------------------------------------------------------------------------
class $modify(EbEnhancedGameObject, EnhancedGameObject) {
    void activatedByPlayer(PlayerObject* player) {
        if (simulating()) {
            // Save pre-touch state for restore. Rings: physics via applyRingJump only —
            // calling the original here re-triggers orb VFX/particles on the real object.
            ShadowSimulator::get().noteActivated(this);
            if (typeinfo_cast<RingObject*>(this)) return;
            EnhancedGameObject::activatedByPlayer(player);
            return;
        }
        EnhancedGameObject::activatedByPlayer(player);
    }

    void powerOnObject(int state) {
        if (simulating()) {
            ShadowSimulator::get().noteActivated(this);
            // Rings: skip — applyRingJump owns orb claim. Pads/other: physics only.
            if (typeinfo_cast<RingObject*>(this)) return;
            EnhancedGameObject::powerOnObject(state);
            return;
        }
        EnhancedGameObject::powerOnObject(state);
    }

    void powerOffObject() {
        if (simulating()) {
            EnhancedGameObject::powerOffObject();
            return;
        }
        EnhancedGameObject::powerOffObject();
    }
};

class $modify(EbEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (simulating()) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(EbGameObject, GameObject) {
    void playShineEffect() {
        if (simulating()) return;
        GameObject::playShineEffect();
    }

    void activateObject() {
        if (simulating()) {
            // Visual / "used" state on the real level object — skip. Physics already applied.
            ShadowSimulator::get().noteActivated(this);
            return;
        }
        GameObject::activateObject();
    }

    void deactivateObject(bool deactivate) {
        if (simulating()) return;
        GameObject::deactivateObject(deactivate);
    }
};

// Orb / pad ripple + power visuals. Real level objects must not show sim touches.
class $modify(EbRingObject, RingObject) {
    void spawnCircle() {
        if (simulating()) return;
        RingObject::spawnCircle();
    }

    void powerOnObject(int state) {
        if (simulating()) {
            ShadowSimulator::get().noteActivated(this);
            return;
        }
        RingObject::powerOnObject(state);
    }

    void triggerActivated(float xPosition) {
        if (simulating()) {
            ShadowSimulator::get().noteActivated(this);
            return;
        }
        RingObject::triggerActivated(xPosition);
    }
};

class $modify(EbHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint point) {
        // Wave collision uses the trail. The shadow must be allowed to grow its own;
        // the real player's trail must not be touched during sim.
        if (simulating() && !ShadowSimulator::get().isShadowTrail(this)) return;
        HardStreak::addPoint(point);
    }
};

// ---------------------------------------------------------------------------
// PlayLayer lifecycle + death detection
// ---------------------------------------------------------------------------
class $modify(EbPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::First);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreatePlayer) {
        if (!PlayLayer::init(level, useReplay, dontCreatePlayer)) return false;
        AssistController::get().onLevelInit(this);
        return true;
    }

    void resetLevel() {
        // Ghosts land in section vectors via sim checkCollisions. Vanilla
        // resetLevel → sortSectionVector then AVs on those bodies — evacuate first.
        PerfTrace::Scope const scope("resetLevelHook");
        LagProbe::get().beginEpisode("resetLevel", fmt::format(
            "attemptTickWas={} practice={}",
            AssistController::get().attemptTick(),
            this->m_isPracticeMode
        ));
        {
            LagProbe::Scope const s("eb.bakeCancel");
            BakeScheduler::get().cancel();
        }
        ShadowSimulator::get().evacuateGhosts(this);
        AssistController::get().resetAttempt(this);
        {
            LagProbe::Scope const s("vanilla.PlayLayer::resetLevel");
            PlayLayer::resetLevel();
        }
        LagProbe::get().endEpisode();
    }

    void onQuit() {
        AssistController::get().onLevelQuit();
        PlayLayer::onQuit();
    }

    // Do NOT hook onExit → onLevelQuit. GD can call onExit around death/retry
    // UI without leaving the level; that tore down ghosts and killed assist
    // until a full re-enter. Quit cleanup stays in onQuit only.

    void destroyPlayer(PlayerObject* player, GameObject* object) {
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
        if (auto* killer = typeinfo_cast<PlayerObject*>(object)) {
            if (sim.isOwnedGhost(killer)) {
                AssistLog::debug("destroyPlayer: swallowed death to owned ghost body");
                if (player) player->m_isDead = false;
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
            if (player) player->m_isDead = false;
            return;
        }

        if (object != m_anticheatSpike) {
            auto const mode = player ? StageConfig::kindName(StageConfig::kindOf(player)) : "?";
            LagProbe::get().beginEpisode("death", fmt::format(
                "x={:.1f} y={:.1f} tick={} mode={} killer={} practice={}",
                player ? player->getPositionX() : 0.f,
                player ? player->getPositionY() : 0.f,
                AssistController::get().attemptTick(),
                mode ? mode : "?",
                object ? object->m_objectID : -1,
                this->m_isPracticeMode
            ));
            AssistController::get().onRealDeath(this, player, object);
            {
                LagProbe::Scope const s("vanilla.destroyPlayer");
                PlayLayer::destroyPlayer(player, object);
            }
            LagProbe::get().endEpisode();
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void playEndAnimationToPos(cocos2d::CCPoint pos) {
        if (simulating()) return;
        PlayLayer::playEndAnimationToPos(pos);
    }

    void playGravityEffect(bool flip) {
        if (simulating()) return;
        PlayLayer::playGravityEffect(flip);
    }

    void spawnCircle() {
        if (simulating()) return;
        PlayLayer::spawnCircle();
    }

    void levelComplete() {
        // Safe Mode OFF → never force test mode (progress like vanilla).
        // Safe Mode ON → enabled or assist-touched runs count as test clears.
        bool const block = StageConfig::blocksProgressSaves(
            AssistController::get().assistTouchedRun()
        );
        if (block) {
            this->m_isTestMode = true;
            AssistLog::debug("Safe Mode ON: levelComplete → testMode (no normal progress)");
        }
        else {
            AssistLog::debug("Safe Mode OFF: levelComplete allows normal progress");
        }
        AssistController::get().onLevelComplete(this);
        PlayLayer::levelComplete();
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (StageConfig::blocksProgressSaves(AssistController::get().assistTouchedRun())) {
            AssistLog::debug("Safe Mode ON: showNewBest swallowed");
            return;
        }
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        LagProbe::get().beginEpisode("loadFromCheckpoint", fmt::format(
            "attemptTickWas={}",
            AssistController::get().attemptTick()
        ));
        {
            LagProbe::Scope const s("eb.bakeCancel");
            BakeScheduler::get().cancel();
        }
        ShadowSimulator::get().evacuateGhosts(this);
        {
            LagProbe::Scope const s("eb.onCheckpointLoad");
            AssistController::get().onCheckpointLoad();
        }
        {
            LagProbe::Scope const s("vanilla.loadFromCheckpoint");
            PlayLayer::loadFromCheckpoint(checkpoint);
        }
        LagProbe::get().endEpisode();
    }
};

class $modify(EbGJGameLevel, GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts, bool isChkValid) {
        // Same contract as PlayLayer::showNewBest / levelComplete (live Safe Mode read).
        if (StageConfig::blocksProgressSaves(AssistController::get().assistTouchedRun())) {
            return;
        }
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
};
