#include "shadow_ghost_lifecycle.hpp"
#include "../orbs/toggle_orb_sim.hpp"
#include "../shadow_simulator.hpp"
#include "../world/shadow_world_capture.hpp"
#include "live_wave_trail_guard.hpp"
#include "shadow_isolation_contracts.hpp"

void EbShadowGhostLifecycle::clearWaveTrailGeometry(PlayerObject* player) {
    if (!player)
        return;
    auto* trail = player->m_waveTrail;
    if (!trail)
        return;
    // Never touch the real player's HardStreak — assist seeds must not wipe it.
    if (EbLiveWaveTrailGuard::refuseMutation(trail))
        return;
    // Owning ghost must still be in the scene graph. After layer teardown /
    // partial destroy, m_waveTrail can be a dangling HardStreak — HardStreak::reset
    // → CCDrawNode::clear then AVs (read @ 0xFFFFFFFFFFFFFFFF).
    if (!player->getParent())
        return;
    if (!trail->getParent())
        return;
    // Prefer point-array clear over reset()/CCDrawNode::clear — safer on ghost
    // streaks that were never stroked or were half-torn by other mods.
    if (trail->m_pointArray) {
        trail->m_pointArray->removeAllObjects();
    }
    trail->m_drawStreak = false;
    trail->setVisible(false);
    trail->setOpacity(0);
}

void EbShadowGhostLifecycle::attach(EbShadowSimulator& simulator, PlayLayer* layer) {
    if (!layer || !layer->m_objectLayer)
        return;
    // Idempotent: LevelInit used to attach twice (resetAttempt + onLevelInit).
    if (simulator.isAttachedTo(layer))
        return;

    simulator.detach();

    // Parent ghosts under a hidden PlayLayer node — never objectLayer. MH hitbox
    // trail walks gameplay objects there; ghosts at sim poses painted the red
    // zigzag that did not match the live white HardStreak.
    auto* ghostRoot = cocos2d::CCNode::create();
    if (!ghostRoot)
        return;
    ghostRoot->setID(EbShadowIsolationContracts::ghostRootId());
    ghostRoot->setVisible(false);
    // Parent under PlayLayer — never objectLayer (MH hitbox trail walks that).
    layer->addChild(ghostRoot);
    simulator.m_ghostRoot = ghostRoot;

    auto makeHidden = [&](char const* id) -> PlayerObject* {
        auto* player = PlayerObject::create(1, 1, layer, layer, true);
        if (!player)
            return nullptr;
        player->setPosition({0.f, 105.f});
        player->setID(id);
        // Explicit marker for other mods (hitbox overlays, etc.): ignore these
        // PlayerObjects — they are sim ghosts, not the real icon.
        EbShadowIsolationContracts::markSimGhostPlayer(player);
        // ghostRoot owns the node (addChild retain). Do NOT extra-retain —
        // release-on-quit while still parented / double-free in ~PlayerObject
        // during setNextScene was the quit AV (CCObject::release).
        ghostRoot->addChild(player);
        // Drop create()'s trail pointer and install a ghostRoot-hosted streak.
        // Never reparent create()'s object — its lifetime is opaque to us.
        player->m_waveTrail = nullptr;
        if (auto* trail = HardStreak::create()) {
            EbShadowIsolationContracts::markSimGhostTrail(trail);
            ghostRoot->addChild(trail);
            player->m_waveTrail = trail;
        }
        simulator.hideGhost(player);
        return player;
    };

    simulator.m_shadow = makeHidden("earlybuffer-shadow-player"_spr);
    for (int index = 0; index < EbShadowSimulator::SNAPSHOT_SLOTS; ++index) {
        auto const id = fmt::format("earlybuffer-snapshot-{}", index);
        simulator.m_snapshots[index] = makeHidden(id.c_str());
    }
    for (int index = 0; index < EbShadowSimulator::DEV_EVAL_SLOTS; ++index) {
        auto const id = fmt::format("earlybuffer-eval-{}", index);
        simulator.m_evalGhosts[index] = makeHidden(id.c_str());
    }
    simulator.m_evalActive = nullptr;
    simulator.m_evalAbort = false;
    simulator.m_nextSnapshot = EbShadowSimulator::SNAPSHOT_RING_BEGIN;
    simulator.m_hostLayer = layer;
    EbShadowWorldCapture::refreshTimeOrigin(simulator, layer);
}

void EbShadowGhostLifecycle::detach(EbShadowSimulator& simulator, bool layerDying) {
    // Never leave the sim flag set — quit can interrupt a traj rebuild mid-capture.
    simulator.m_simulating = false;
    simulator.m_bakeOffline = false;
    simulator.m_applyingRing = false;
    simulator.m_orbScanning = false;
    simulator.m_orbOverlapTick = false;
    simulator.m_suppressDeathHitboxes = false;
    simulator.m_savedHitboxesOnDeath = false;
    simulator.m_savedP1Dead = false;
    simulator.m_savedP2Dead = false;
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;
    simulator.m_simAbsTick = -1;
    simulator.m_simTickBase = 0;
    simulator.m_triggerOriginLayer = nullptr;
    simulator.m_captureObjects.clear();
    simulator.m_captureEffects.clear();
    simulator.m_captureIndexKey.reset();
    simulator.m_transformCaptureLayer = nullptr;
    simulator.m_triggerWorldAppliedIds.clear();
    simulator.m_poseRemapDt = -1.f;
    simulator.m_triggerWorldAppliedThisTick = false;
    simulator.m_dynamicTargetIds.clear();
    simulator.m_toggleTargetIds.clear();
    simulator.m_dynamicFollowJobs.clear();
    simulator.m_liveDynamicAppliedIds.clear();
    EbToggleOrbSim::get().restore();
    simulator.m_saved.clear();
    simulator.m_savedTransforms.clear();
    // gd::map: `= {}` is ambiguous on libstdc++/Android — use clear().
    simulator.m_savedActivatedIDs.clear();
    // Capture host before clear — nested evacuate must see "not attached".
    auto* const previousHost = simulator.m_hostLayer;
    simulator.m_hostLayer = nullptr;

    // Pause→Editor→Save&Play (and similar): PlayLayer is destroyed while ghost
    // pointers may still sit on the simulator. Touching those PlayerObjects
    // (getParent / removeFromParent) AVs with read @ 0xFFFFFFFFFFFFFFFF.
    // Only destroy when the recorded host is still the live PlayLayer.
    auto* const livePlay = PlayLayer::get();
    bool const abandonGhosts = layerDying || previousHost != livePlay;

    auto sanitize = [](PlayerObject* player) {
        if (!player)
            return;
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        if (player->m_touchingRings) {
            player->m_touchingRings->removeAllObjects();
        }
        // Ghost HardStreaks live under ghostRoot (not under the player). Clear
        // raw pointers before ~PlayerObject so quit cannot double-release the
        // same node via player dtor + ghostRoot removeChild.
        player->m_waveTrail = nullptr;
        player->m_regularTrail = nullptr;
        player->m_shipStreak = nullptr;
        // Do NOT HardStreak::reset here — detach always destroys or abandons the
        // node next; reset on a dying/ghost streak was the LevelInit AV.
        player->setVisible(false);
    };

    auto kill = [&](PlayerObject*& player) {
        if (!player)
            return;
        if (abandonGhosts) {
            // Parent tree gone / wrong layer — drop the pointer only.
            player = nullptr;
            return;
        }
        sanitize(player);
        // Normal quit / same-layer reattach: destroy while PlayLayer is alive.
        if (player->getParent()) {
            player->removeFromParentAndCleanup(true);
        }
        player = nullptr;
    };
    kill(simulator.m_shadow);
    for (auto& s : simulator.m_snapshots)
        kill(s);
    for (auto& e : simulator.m_evalGhosts)
        kill(e);
    simulator.m_evalActive = nullptr;
    simulator.m_evalAbort = false;
    if (simulator.m_ghostRoot) {
        if (!abandonGhosts && simulator.m_ghostRoot->getParent())
            simulator.m_ghostRoot->removeFromParentAndCleanup(true);
        simulator.m_ghostRoot = nullptr;
    }
    EbLiveWaveTrailGuard::clear();
}

bool EbShadowGhostLifecycle::isOwnedGhost(EbShadowSimulator const& simulator, PlayerObject* player) {
    if (!player)
        return false;
    // Pointer table only — never getUserObject here. During setNextScene
    // teardown the node may be half-destroyed; marker reads AVs.
    if (player == simulator.m_shadow)
        return true;
    for (auto* shadowPlayer : simulator.m_snapshots) {
        if (player == shadowPlayer)
            return true;
    }
    for (auto* entry : simulator.m_evalGhosts) {
        if (player == entry)
            return true;
    }
    return false;
}

void EbShadowGhostLifecycle::parkGhosts(EbShadowSimulator& simulator) {
    auto scrub = [&](PlayerObject* player) {
        if (!player)
            return;
        if (auto* layer = PlayLayer::get()) {
            if (layer == simulator.m_hostLayer) {
                EbShadowSimulator::tryRemoveFromSection(layer, player);
            }
        }
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        // Snapshots can die during capture — never leave m_isDead for menus.
        player->m_isDead = false;
        simulator.hideGhost(player);
    };
    auto parkOffLevel = [&](PlayerObject* player) {
        if (!player)
            return;
        scrub(player);
        player->setPosition({0.f, 105.f});
        player->m_position = cocos2d::CCPoint{0.f, 105.f};
        player->m_yVelocity = 0.0;
        player->m_isDead = false;
        // Wave trail points stay in world space after a sim — clear so leftover
        // streak geometry cannot interact with live collision/debug paths.
        EbShadowGhostLifecycle::clearWaveTrailGeometry(player);
    };

    parkOffLevel(simulator.m_shadow);
    for (auto* effectObject : simulator.m_evalGhosts) {
        parkOffLevel(effectObject);
    }
    // Snapshots keep captured pose for bake / traj, but their HardStreak point
    // arrays must not remain drawable in objectLayer (wave-only visual junk).
    for (auto* slot : simulator.m_snapshots) {
        scrub(slot);
        EbShadowGhostLifecycle::clearWaveTrailGeometry(slot);
    }
}

void EbShadowGhostLifecycle::beginSession(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_simulating = true;
    simulator.m_applyingRing = false;
    // Vanilla / MH "hitboxes on death" keys off these — freeze them for the sim.
    simulator.m_savedHitboxesOnDeath = false;
    simulator.m_savedP1Dead = false;
    simulator.m_savedP2Dead = false;
    if (layer) {
        simulator.m_savedHitboxesOnDeath = layer->m_hitboxesOnDeath;
        layer->m_hitboxesOnDeath = false;
        if (layer->m_player1)
            simulator.m_savedP1Dead = layer->m_player1->m_isDead;
        if (layer->m_player2)
            simulator.m_savedP2Dead = layer->m_player2->m_isDead;
        // Hide RobTop debug draw for the session so ghost section poses cannot
        // flash hazard hitboxes mid-plan (MH patches keep the draw path hot).
        if (layer->m_debugDrawNode)
            layer->m_debugDrawNode->setVisible(false);
    }
    simulator.beginCapture(layer);
    // Mute once for the session; stepPhysics no longer re-mutes every tick.
    simulator.hideGhost(simulator.m_shadow);
    if (simulator.m_evalActive)
        simulator.hideGhost(simulator.m_evalActive);
}

void EbShadowGhostLifecycle::endSession(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_simAbsTick = -1;
    simulator.m_poseRemapDt = -1.f;
    simulator.m_bakeOffline = false;
    if (layer) {
        simulator.restoreCapture(layer);
        layer->m_hitboxesOnDeath = simulator.m_savedHitboxesOnDeath;
        // Sim must not leave the real icons looking dead to hitbox-on-death menus.
        if (layer->m_player1 && !simulator.m_savedP1Dead)
            layer->m_player1->m_isDead = false;
        if (layer->m_player2 && !simulator.m_savedP2Dead)
            layer->m_player2->m_isDead = false;
        if (layer->m_debugDrawNode && layer->m_player1 && !layer->m_player1->m_isDead &&
            !layer->m_isDebugDrawEnabled) {
            // Keep suppressed after ghost deaths; MH postUpdate may re-show —
            // PlayLayer::postUpdate (Last) enforces while suppressDeathHitboxes.
            if (simulator.m_suppressDeathHitboxes)
                layer->m_debugDrawNode->setVisible(false);
        }
    } else {
        // Still drop claim deltas / flags if the layer vanished mid-sim.
        EbToggleOrbSim::get().restore();
        simulator.m_saved.clear();
        simulator.m_savedTransforms.clear();
        simulator.m_dynamicTargetIds.clear();
        simulator.m_toggleTargetIds.clear();
        simulator.m_dynamicFollowJobs.clear();
        simulator.m_liveDynamicAppliedIds.clear();
        simulator.m_toggleUnknownHit = false;
        simulator.m_triggerWorldAppliedThisTick = false;
    }
    simulator.m_simulating = false;
    simulator.m_applyingRing = false;
    simulator.parkGhosts();
}

void EbShadowGhostLifecycle::hideGhost(PlayerObject* player) {
    if (!player)
        return;
    player->setVisible(false);
    player->setOpacity(0);
    player->m_playEffects = false;
    // Only mute the ghost's own trail — never the protected live HardStreak.
    if (player->m_waveTrail && player->getParent() && player->m_waveTrail->getParent() &&
        !EbLiveWaveTrailGuard::refuseMutation(player->m_waveTrail)) {
        EbShadowIsolationContracts::markSimGhostTrail(player->m_waveTrail);
        player->m_waveTrail->m_drawStreak = false;
        player->m_waveTrail->setVisible(false);
        player->m_waveTrail->setOpacity(0);
    }
    if (player->m_regularTrail) {
        player->m_regularTrail->setVisible(false);
        player->m_regularTrail->setOpacity(0);
    }
    if (player->m_shipStreak) {
        player->m_shipStreak->setVisible(false);
        player->m_shipStreak->setOpacity(0);
    }
    // Shadow particle emitters must not paint the real level during sim.
    auto mute = [](cocos2d::CCParticleSystemQuad* queryOffset) {
        if (!queryOffset)
            return;
        queryOffset->setVisible(false);
        queryOffset->stopSystem();
        queryOffset->setEmissionRate(0.f);
    };
    mute(player->m_playerGroundParticles);
    mute(player->m_trailingParticles);
    mute(player->m_shipClickParticles);
    mute(player->m_vehicleGroundParticles);
    mute(player->m_ufoClickParticles);
    mute(player->m_robotBurstParticles);
    mute(player->m_dashParticles);
    mute(player->m_swingBurstParticles1);
    mute(player->m_swingBurstParticles2);
    mute(player->m_landParticles0);
    mute(player->m_landParticles1);
}
