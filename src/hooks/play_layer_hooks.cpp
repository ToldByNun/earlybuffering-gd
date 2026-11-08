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
    if (GameHookSimulationGuard::simulating())
        return;
    PlayLayer::playEndAnimationToPos(position);
}

auto EbPlayLayer::playGravityEffect(bool flip) -> void {
    if (GameHookSimulationGuard::simulating())
        return;
    PlayLayer::playGravityEffect(flip);
}

auto EbPlayLayer::spawnCircle() -> void {
    if (GameHookSimulationGuard::simulating())
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
