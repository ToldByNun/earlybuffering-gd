#include "shadow_ghost_lifecycle.hpp"
#include "../orbs/toggle_orb_sim.hpp"
#include "../shadow_simulator.hpp"

void EbShadowGhostLifecycle::attach(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.detach();
    if (!layer || !layer->m_objectLayer)
        return;

    auto makeHidden = [&](char const* id) -> PlayerObject* {
        auto* player = PlayerObject::create(1, 1, layer, layer, true);
        if (!player)
            return nullptr;
        player->setPosition({0.f, 105.f});
        player->setID(id);
        // objectLayer owns the node (addChild retain). Do NOT extra-retain —
        // release-on-quit while still parented / double-free in ~PlayerObject
        // during setNextScene was the quit AV (CCObject::release).
        layer->m_objectLayer->addChild(player);
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
}

void EbShadowGhostLifecycle::detach(EbShadowSimulator& simulator, bool layerDying) {
    // Never leave the sim flag set — quit can interrupt a traj rebuild mid-capture.
    simulator.m_simulating = false;
    simulator.m_bakeOffline = false;
    simulator.m_applyingRing = false;
    simulator.m_orbScanning = false;
    simulator.m_orbOverlapTick = false;
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;
    simulator.m_simAbsTick = -1;
    simulator.m_simTickBase = 0;
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
    // Clear host first so a nested evacuate (if any) sees "not attached".
    simulator.m_hostLayer = nullptr;

    auto sanitize = [](PlayerObject* player) {
        if (!player)
            return;
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        if (player->m_touchingRings) {
            player->m_touchingRings->removeAllObjects();
        }
        if (player->m_waveTrail) {
            player->m_waveTrail->reset();
        }
        player->setVisible(false);
    };

    auto kill = [&](PlayerObject*& player) {
        if (!player)
            return;
        sanitize(player);
        if (layerDying) {
            // Parent tree is already tearing down — do not removeFromParent /
            // release (double-free in ~PlayerObject during setNextScene).
            player = nullptr;
            return;
        }
        // Normal quit / reattach: destroy while PlayLayer is still alive.
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
}

bool EbShadowGhostLifecycle::isOwnedGhost(EbShadowSimulator const& simulator, PlayerObject* player) {
    if (!player)
        return false;
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
        if (player->m_waveTrail) {
            player->m_waveTrail->reset();
        }
    };

    parkOffLevel(simulator.m_shadow);
    for (auto* effectObject : simulator.m_evalGhosts) {
        parkOffLevel(effectObject);
    }
    // Snapshots must keep captured pose for bake / outcome verify / traj
    // samplePath — scrub+hide only so they leave section vectors.
    for (auto* slot : simulator.m_snapshots) {
        scrub(slot);
    }
}

void EbShadowGhostLifecycle::beginSession(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_simulating = true;
    simulator.m_applyingRing = false;
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
    if (player->m_waveTrail) {
        player->m_waveTrail->setVisible(false);
        player->m_waveTrail->setOpacity(0);
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
