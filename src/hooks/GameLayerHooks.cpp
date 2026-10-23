#include "../core/AssistController.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"
#include "../ui/StatusOverlay.hpp"

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
using namespace frire;

namespace {
inline bool simulating() { return ShadowSimulator::get().isSimulating(); }
}

// ---------------------------------------------------------------------------
// Input + physics tick
// ---------------------------------------------------------------------------
class $modify(FrireGJBaseGameLayer, GJBaseGameLayer) {
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
    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (simulating()) {
            auto& sim = ShadowSimulator::get();
            if (sim.isOrbScanning()) {
                sim.noteOrbOverlap();
                return;
            }
            sim.applyRingJump(player, ring);
            return;
        }
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (simulating()) return;
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
        if (!simulating()) {
            GJBaseGameLayer::collisionCheckObjects(player, vec, objectsCount, dt);
            return;
        }
        // Only physics-relevant objects: solids, hazards, slopes, pads, orbs,
        // gravity + size portals (player-local). Mode/speed/teleport/dual portals,
        // triggers, breakables and collectibles have global side effects → skipped.
        gd::vector<GameObject*> extra;
        extra.reserve(objectsCount);
        for (int i = 0; i < objectsCount; i++) {
            GameObject* obj = vec->at(i);
            switch (obj->m_objectType) {
                case GameObjectType::Solid:
                case GameObjectType::Hazard:
                case GameObjectType::AnimatedHazard:
                case GameObjectType::Slope:
                case GameObjectType::InverseGravityPortal:
                case GameObjectType::NormalGravityPortal:
                case GameObjectType::GravityTogglePortal:
                case GameObjectType::RegularSizePortal:
                case GameObjectType::MiniSizePortal:
                case GameObjectType::YellowJumpPad:
                case GameObjectType::PinkJumpPad:
                case GameObjectType::RedJumpPad:
                case GameObjectType::GravityPad:
                case GameObjectType::SpiderPad:
                case GameObjectType::YellowJumpRing:
                case GameObjectType::PinkJumpRing:
                case GameObjectType::RedJumpRing:
                case GameObjectType::GravityRing:
                case GameObjectType::GreenRing:
                case GameObjectType::DropRing:
                case GameObjectType::DashRing:
                case GameObjectType::GravityDashRing:
                case GameObjectType::SpiderOrb:
                case GameObjectType::CustomRing:
                case GameObjectType::CollisionObject:
                case GameObjectType::Breakable:
                    extra.push_back(obj);
                    break;

                // Portals we deliberately don't simulate: if the shadow actually reaches
                // one, everything beyond is unknown → stop the run instead of guessing.
                case GameObjectType::ShipPortal:
                case GameObjectType::CubePortal:
                case GameObjectType::BallPortal:
                case GameObjectType::UfoPortal:
                case GameObjectType::WavePortal:
                case GameObjectType::RobotPortal:
                case GameObjectType::SpiderPortal:
                case GameObjectType::SwingPortal:
                case GameObjectType::InverseMirrorPortal:
                case GameObjectType::NormalMirrorPortal:
                case GameObjectType::DualPortal:
                case GameObjectType::SoloPortal:
                case GameObjectType::TeleportPortal:
                case GameObjectType::TeleportOrb:
                case GameObjectType::Modifier:
                    if (player && obj->getObjectRect().intersectsRect(player->getObjectRect())) {
                        ShadowSimulator::get().noteUnknown();
                    }
                    break;
                default:
                    break;
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &extra, static_cast<int>(extra.size()), dt);
    }
};

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
class $modify(FrirePlayerObject, PlayerObject) {
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
        if (!simulating()) {
            if (auto* pl = PlayLayer::get(); pl && this == pl->m_player1) {
                auto& ac = AssistController::get();
                ac.onPhysicsTick(pl, dt);
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
class $modify(FrirePlayerObjectLate, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::update", Priority::Last);
    }

    void update(float dt) {
        if (!simulating()) AssistController::get().onSubStep(this, dt);
        PlayerObject::update(dt);
    }
};

// ---------------------------------------------------------------------------
// Objects touched by the shadow: no triggers, no visuals; flags restored after sim
// ---------------------------------------------------------------------------
class $modify(FrireEnhancedGameObject, EnhancedGameObject) {
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

class $modify(FrireEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (simulating()) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(FrireGameObject, GameObject) {
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
class $modify(FrireRingObject, RingObject) {
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

class $modify(FrireHardStreak, HardStreak) {
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
class $modify(FrirePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::First);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreatePlayer) {
        if (!PlayLayer::init(level, useReplay, dontCreatePlayer)) return false;
        AssistController::get().onLevelInit(this);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        AssistController::get().resetAttempt(this);
    }

    void onQuit() {
        AssistController::get().onLevelQuit();
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (simulating() && object != m_anticheatSpike && ShadowSimulator::get().isShadow(player)) {
            ShadowSimulator::get().markDead(player, object);
            return;
        }
        if (!simulating() && object != m_anticheatSpike) {
            AssistController::get().onRealDeath(this, player, object);
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
        if (StageConfig::safeMode() && StageConfig::isEnabled()) {
            m_isTestMode = true;
        }
        PlayLayer::levelComplete();
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (StageConfig::safeMode() && StageConfig::isEnabled()) return;
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }
};

class $modify(FrireGJGameLevel, GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts, bool isChkValid) {
        if (StageConfig::safeMode() && StageConfig::isEnabled()) return;
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
};
