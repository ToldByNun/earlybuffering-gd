#include "../app/class_manager.hpp"
#include "play_layer_hooks.hpp"
#include "../config/stage_config.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/bake/bake_scheduler.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "game_hook_simulation_guard.hpp"

auto EbPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreatePlayer) -> bool {
    if (!PlayLayer::init(level, useReplay, dontCreatePlayer))
        return false;
    EbClassManager::get().onLevelInit(this);
    return true;
}

auto EbPlayLayer::resetLevel() -> void {
    // Ghosts land in section vectors via sim checkCollisions. Vanilla
    // resetLevel → sortSectionVector then AVs on those bodies — evacuate first.
    EbPerfTrace::Scope const scope("resetLevelHook");
    EbLagProbe::get().beginEpisode("resetLevel", fmt::format("attemptTickWas={} practice={}",
                                                           EbAssistController::get().attemptTick(),
                                                           this->m_isPracticeMode));
    EbShadowSimulator::get().clearDeathHitboxSuppress();
    EbClassManager::get().beforeLevelReset(this);
    {
        EbLagProbe::Scope const state("vanilla.PlayLayer::resetLevel");
        PlayLayer::resetLevel();
    }
    EbLagProbe::get().endEpisode();
}

auto EbPlayLayer::onQuit() -> void {
    EbClassManager::get().onLevelQuit(this);
    PlayLayer::onQuit();
}

auto EbPlayLayer::destroyPlayer(PlayerObject* player, GameObject* object) -> void {
    auto& sim = EbShadowSimulator::get();

    // Ghosts (shadow + snapshots) must NEVER enter vanilla destroyPlayer.
    // That path can fail the real attempt — looks like "death to nothing"
    // because the death effect is on m_player1 while the collision was on
    // an invisible ghost left at a sim/hazard position.
    // Also covers anticheatSpike: only real players may take that path.
    if (sim.isOwnedGhost(player)) {
        if (sim.isSimulating()) {
            sim.markDead(player, object);
        } else {
            sim.noteGhostDeathHitboxSuppress();
        }
        // Even if not simulating (stale collide): never look "dead" to menus.
        if (player)
            player->m_isDead = false;
        // Real icons must stay "alive" for MH Hitboxes::destroyPlayer which
        // checks m_player1->m_isDead after Parent returns (when MH runs first).
        sim.clearContaminatedRealDeathFlags(this);
        // Do not call PlayLayer::destroyPlayer — Geode hooks after us never run.
        // Standalone MH trampolines that wrap the whole chain still get a call;
        // suppressDeathHitboxes + postUpdate Last hides their false latch.
        return;
    }

    // Real player colliding with an owned ghost-as-object (wave vs invisible
    // snapshot/shadow body) — treat as non-event, not a level fail.
    if (auto* killer = geode::prelude::typeinfo_cast<PlayerObject*>(object)) {
        if (sim.isOwnedGhost(killer)) {
            EbAssistLog::debug("destroyPlayer: swallowed death to owned ghost body");
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

    // Real death — allow hitbox-on-death menus again.
    sim.clearDeathHitboxSuppress();

    if (object != m_anticheatSpike) {
        auto const mode = player ? EbStageConfig::kindName(EbStageConfig::kindOf(player)) : "?";
        EbLagProbe::get().beginEpisode(
            "death", fmt::format("x={:.1f} y={:.1f} tick={} mode={} killer={} practice={}",
                                 player ? player->getPositionX() : 0.f,
                                 player ? player->getPositionY() : 0.f,
                                 EbAssistController::get().attemptTick(), mode ? mode : "?",
                                 object ? object->m_objectID : -1, this->m_isPracticeMode));
        EbAssistController::get().onRealDeath(this, player, object);
        {
            EbLagProbe::Scope const state("vanilla.destroyPlayer");
            PlayLayer::destroyPlayer(player, object);
        }
        EbLagProbe::get().endEpisode();
        return;
    }
    PlayLayer::destroyPlayer(player, object);
}

auto EbPlayLayer::postUpdate(float deltaTime) -> void {
    PlayLayer::postUpdate(deltaTime);
    auto& sim = EbShadowSimulator::get();
    // Mega Hack / OpenHack: Hitboxes::destroyPlayer may latch s_isDead on any
    // destroyPlayer call (including ghosts), then postUpdate re-shows
    // m_debugDrawNode while the real icon is still alive. Undo that.
    if (!sim.suppressDeathHitboxes())
        return;
    if (!this->m_player1 || this->m_player1->m_isDead)
        return;
    if (this->m_isDebugDrawEnabled && this->m_isPracticeMode)
        return;
    if (this->m_debugDrawNode)
        this->m_debugDrawNode->setVisible(false);
}

auto EbPlayLayer::playEndAnimationToPos(cocos2d::CCPoint position) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayLayer::playEndAnimationToPos(position);
}

auto EbPlayLayer::playGravityEffect(bool flip) -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayLayer::playGravityEffect(flip);
}

auto EbPlayLayer::spawnCircle() -> void {
    if (EbGameHookSimulationGuard::simulating())
        return;
    PlayLayer::spawnCircle();
}

auto EbPlayLayer::levelComplete() -> void {
    // Safe Mode OFF → never force test mode (progress like vanilla).
    // Safe Mode ON → enabled or assist-touched runs count as test clears.
    bool const block = EbStageConfig::blocksProgressSaves(EbAssistController::get().assistTouchedRun());
    if (block) {
        this->m_isTestMode = true;
        EbAssistLog::debug("Safe Mode ON: levelComplete → testMode (no normal progress)");
    } else {
        EbAssistLog::debug("Safe Mode OFF: levelComplete allows normal progress");
    }
    EbClassManager::get().onLevelComplete(this);
    PlayLayer::levelComplete();
}

auto EbPlayLayer::showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry,
                              bool noTitle) -> void {
    if (EbStageConfig::blocksProgressSaves(EbAssistController::get().assistTouchedRun())) {
        EbAssistLog::debug("Safe Mode ON: showNewBest swallowed");
        return;
    }
    PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
}

auto EbPlayLayer::loadFromCheckpoint(CheckpointObject* checkpoint) -> void {
    EbLagProbe::get().beginEpisode(
        "loadFromCheckpoint",
        fmt::format("attemptTickWas={}", EbAssistController::get().attemptTick()));
    EbClassManager::get().onCheckpointLoad(this);
    {
        EbLagProbe::Scope const state("vanilla.loadFromCheckpoint");
        PlayLayer::loadFromCheckpoint(checkpoint);
    }
    EbLagProbe::get().endEpisode();
}

auto EbGJGameLevel::savePercentage(int percent, bool isPracticeMode, int clicks, int attempts,
                                   bool isChkValid) -> void {
    // Same contract as PlayLayer::showNewBest / levelComplete (live Safe Mode read).
    if (EbStageConfig::blocksProgressSaves(EbAssistController::get().assistTouchedRun())) {
        return;
    }
    GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
}
