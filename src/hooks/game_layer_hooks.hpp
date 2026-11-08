#ifndef GAME_LAYER_HOOKS_HPP
#define GAME_LAYER_HOOKS_HPP

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

class GameLayerHooksImplementation {
public:
    static inline bool simulating();

private:
    GameLayerHooksImplementation() = delete;
};

class $modify(EbGJBaseGameLayer, GJBaseGameLayer) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::handleButton",
                                   geode::prelude::Priority::First);
    }

    void handleButton(bool down, int button, bool isPlayer1);

    // --- simulation guards ---
    void flipGravity(PlayerObject * player, bool flip, bool noEffects);

    void gameEventTriggered(GJGameEvent event, int material, int playerID);

    bool canBeActivatedByPlayer(PlayerObject * player, EffectGameObject * object);

    // Orbs: one physics jump on the shadow, claim for this run only, no VFX.
    // Requires Jump held (applyRingJump) — wait-ticks must not claim early.
    // Orb-window scan records overlap only (no boost / claim).
    // Toggle orb 1594: reversible group flip via ToggleOrbSim (no triggerObject).
    // Commit 7: Toggle+normal stack → BOTH fire (vanilla clicks both partners).
    void playerTouchedRing(PlayerObject * player, RingObject * ring);

    void playerTouchedTrigger(PlayerObject * player, EffectGameObject * object);

    // Pads: allow the boost for the shadow; VFX hooks below stay blocked.
    void bumpPlayer(PlayerObject * player, EffectGameObject * object);

    cocos2d::CCParticleSystemQuad* spawnParticle(char const* plist, int zOrder,
                                                 cocos2d::tCCPositionType positionType,
                                                 cocos2d::CCPoint position);

    cocos2d::CCParticleSystemQuad* createParticle(int objectType, char const* plistName, int tag,
                                                  cocos2d::tCCPositionType positionType);

    cocos2d::CCParticleSystemQuad* claimParticle(gd::string key, int zLayer);

    cocos2d::CCParticleSystemQuad* createCustomParticle(
        gd::string const& key, cocos2d::ParticleStruct const& particleStruct, int minimum,
        bool dontAdd);

    cocos2d::CCParticleSystemQuad* claimCustomParticle(
        gd::string const& key, cocos2d::ParticleStruct const& particleStruct, int zLayer,
        int zOrder, int uiObject, bool dontAdd);

    void spawnParticleTrigger(int particleID, cocos2d::CCPoint position, float rotation,
                              float scale);

    void collisionCheckObjects(PlayerObject * player, gd::vector<GameObject*> * vec,
                               int objectsCount, float deltaTime);

private:
};

class $modify(EbPlayerObject, PlayerObject) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::pushButton", geode::prelude::Priority::First);
        (void)self.setHookPriority("PlayerObject::update", geode::prelude::Priority::First);
    }

    bool pushButton(PlayerButton button);

    void update(float deltaTime);

    void incrementJumps();

    void ringJump(RingObject * ring, bool skipCheck);

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

class $modify(EbEnhancedGameObject, EnhancedGameObject) {
public:
    void activatedByPlayer(PlayerObject * player);

    void powerOnObject(int state);

    void powerOffObject();

private:
};

class $modify(EbEffectGameObject, EffectGameObject) {
public:
    void triggerObject(GJBaseGameLayer * layer, int uniqueID, gd::vector<int> const* remapKeys);

private:
};

class $modify(EbGameObject, GameObject) {
public:
    void playShineEffect();

    void activateObject();

    void deactivateObject(bool deactivate);

private:
};

class $modify(EbRingObject, RingObject) {
public:
    void spawnCircle();

    void powerOnObject(int state);

    void triggerActivated(float xPosition);

private:
};

class $modify(EbHardStreak, HardStreak) {
public:
    void addPoint(cocos2d::CCPoint point);

private:
};

class $modify(EbPlayLayer, PlayLayer) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", geode::prelude::Priority::First);
    }

    bool init(GJGameLevel * level, bool useReplay, bool dontCreatePlayer);

    void resetLevel();

    void onQuit();

    // Do NOT hook onExit → onLevelQuit. GD can call onExit around death/retry
    // UI without leaving the level; that tore down ghosts and killed assist
    // until a full re-enter. Quit cleanup stays in onQuit only.

    void destroyPlayer(PlayerObject * player, GameObject * object);

    void playEndAnimationToPos(cocos2d::CCPoint position);

    void playGravityEffect(bool flip);

    void spawnCircle();

    void levelComplete();

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry,
                     bool noTitle);

    void loadFromCheckpoint(CheckpointObject * checkpoint);

private:
};

class $modify(EbGJGameLevel, GJGameLevel) {
public:
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts,
                        bool isChkValid);

private:
};

#endif
