#ifndef GAME_LAYER_HOOKS_HPP
#define GAME_LAYER_HOOKS_HPP

#include "../config/stage_config.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/bake/bake_scheduler.hpp"
#include "../core/sim/orbs/orb_stack_resolver.hpp"
#include "../core/sim/orbs/toggle_orb_sim.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/sim/sim_collision_policy.hpp"
#include "../core/trigger_world/trigger_classify.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../ui/overlays/status_overlay.hpp"
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

class $modify(EbGJBaseGameLayer, GJBaseGameLayer) {
public:
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::handleButton",
                                   geode::prelude::Priority::First);
    }

    void handleButton(bool down, int button, bool isPlayer1);

    // --- simulation guards ---
    void flipGravity(PlayerObject* player, bool flip, bool noEffects);

    void gameEventTriggered(GJGameEvent event, int material, int playerID);

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object);

    // Orbs: one physics jump on the shadow, claim for this run only, no VFX.
    // Requires Jump held (applyRingJump) — wait-ticks must not claim early.
    // Orb-window scan records overlap only (no boost / claim).
    // Toggle orb 1594: reversible group flip via ToggleOrbSim (no triggerObject).
    // Commit 7: Toggle+normal stack → BOTH fire (vanilla clicks both partners).
    void playerTouchedRing(PlayerObject* player, RingObject* ring);

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object);

    // Pads: allow the boost for the shadow; VFX hooks below stay blocked.
    void bumpPlayer(PlayerObject* player, EffectGameObject* object);

    cocos2d::CCParticleSystemQuad* spawnParticle(char const* plist, int zOrder,
                                                 cocos2d::tCCPositionType positionType,
                                                 cocos2d::CCPoint position);

    cocos2d::CCParticleSystemQuad* createParticle(int objectType, char const* plistName, int tag,
                                                  cocos2d::tCCPositionType positionType);

    cocos2d::CCParticleSystemQuad* claimParticle(gd::string key, int zLayer);

    cocos2d::CCParticleSystemQuad*
    createCustomParticle(gd::string const& key, cocos2d::ParticleStruct const& particleStruct,
                         int minimum, bool dontAdd);

    cocos2d::CCParticleSystemQuad*
    claimCustomParticle(gd::string const& key, cocos2d::ParticleStruct const& particleStruct,
                        int zLayer, int zOrder, int uiObject, bool dontAdd);

    void spawnParticleTrigger(int particleID, cocos2d::CCPoint position, float rotation,
                              float scale);

    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* vec, int objectsCount,
                               float deltaTime);

private:
};

#endif
