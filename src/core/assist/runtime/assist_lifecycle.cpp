#include "assist_lifecycle.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../ui/feedback/save_feedback.hpp"
#include "../../../ui/feedback/save_frame_counter.hpp"
#include "../../../ui/overlays/status_overlay.hpp"
#include "../../../ui/visualizers/bake_path_visualizer.hpp"
#include "../../../ui/visualizers/trajectory_visualizer.hpp"
#include "../../bake/bake_map.hpp"
#include "../../bake/bake_verifier.hpp"
#include "../../bake/hybrid_verify_cache.hpp"
#include "../../bake/self_bake_recorder.hpp"
#include "../../diagnostics/assist_real_metrics.hpp"
#include "../../diagnostics/divergence_tracker.hpp"
#include "../../diagnostics/ghost_parity_probe.hpp"
#include "../../learning/level_profile.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../../sim/sim_trust.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../assist_clock.hpp"
#include "../assist_controller.hpp"
#include "../input/click_intent_guard.hpp"

void AssistLifecycle::onCheckpointLoad(AssistController& controller) {
    // Do not flush/refine here — idle drain handles the buffer after respawn.
    // Trigger world: keep store + in-flight baker; only reset abs attempt tick.
    SelfBakeRecorder::get().flushSegment(PlayLayer::get(), "checkpoint", false);
    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseArmTick = 0;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseTick = 0;
    controller.m_outcomePending = false;
    controller.m_attemptTick = 0;
    controller.m_startSnapshotTaken = false;
    controller.m_startSnapshotSlot = -1;
    ClickIntentGuard::get().reset();
    DevCycleRecorder::get().noteCheckpoint(0);
    LagProbe::get().mark("triggerWorld.checkpoint", "keep store / no rebuild / attemptTick=0");
}

void AssistLifecycle::onLevelComplete(AssistController& controller, PlayLayer* playLayer) {
    DevCycleRecorder::get().sealAttemptWithOutcomes("complete", true, controller.m_attemptTick);
    SelfBakeRecorder::get().flushMergeOnly(playLayer, "complete");
    LevelProfile::get().save();
    BakeMap::get().save();
    {
        LagProbe::Scope const state("eb.triggerBake.save");
        TriggerWorldStore::get().save();
    }
}

void AssistLifecycle::syncButtonState(AssistController& controller, PlayLayer* playLayer) {
    if (!playLayer)
        return;
    PlayerObject* players[2] = {playLayer->m_player1, playLayer->m_player2};
    for (int index = 0; index < 2; ++index) {
        auto* player = players[index];
        if (!player)
            continue;
        // Extend-hold after user release: Jump stays down until flushPendingRelease.
        if (controller.m_pendingRelease && controller.m_pendingReleaseP1 == (index == 0) &&
            !controller.m_userHolding[index]) {
            continue;
        }
        auto it = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        bool const gdDown = it != player->m_holdingButtons.end() && it->second;
        if (gdDown && !controller.m_userHolding[index]) {
            controller.m_injecting = true;
            player->releaseButton(PlayerButton::Jump);
            controller.m_injecting = false;
            AssistLog::debug(
                fmt::format("button sync: released stuck jump button (p{})", index + 1));
        }
    }
}

void AssistLifecycle::resetAttempt(AssistController& controller, PlayLayer* playLayer) {
    // Self-bake stays buffered — idle drain merges after we're alive again.
    LagProbe::Scope const episodeScope("eb.resetAttempt");
    {
        LagProbe::Scope const state("eb.flushSegment");
        SelfBakeRecorder::get().flushSegment(playLayer, "reset", false);
    }
    // If death already sealed the attempt, open the next; else seal as reset.
    DevCycleRecorder::get().onAttemptReset(0);
    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseArmTick = 0;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseTick = 0;
    controller.m_assistTouchedRun = false;
    controller.m_savesAttempt = 0;
    DivergenceTracker::get().clear();
    HybridVerifyCache::get().clearAttempt();
    SimTrust::get().resetAttempt();
    GhostParityProbe::get().resetAttempt();
    controller.m_attemptTick = 0;
    controller.m_inRealUpdate = false;
    controller.m_subDtSeen = 0.f;
    controller.m_dtEma = 1.f / 240.f;
    controller.m_dtVarEma = 0.f;
    controller.m_modeKnown = false;
    controller.m_startSnapshotTaken = false;
    controller.m_startSnapshotSlot = -1;
    controller.m_lastBakeHint.clear();
    controller.m_log.clear();
    controller.m_clickTimes.clear();
    controller.m_cpsLocked = false;
    controller.m_outcomePending = false;
    controller.m_outcomePlanDelay = false;
    controller.m_untrustPassHint = false;
    controller.m_dafWatch = false;
    {
        LagProbe::Scope const state("eb.saveFeedbackReset");
        SaveFeedback::get().reset();
    }
    {
        LagProbe::Scope const state("eb.clickGuardReset");
        ClickIntentGuard::get().reset();
    }
    {
        LagProbe::Scope const state("eb.syncButtons");
        controller.syncButtonState(playLayer);
    }
    {
        LagProbe::Scope const state("eb.updateOverlay");
        controller.updateOverlay();
    }
    // If ghosts were torn down mid-attempt, re-attach so assist works again.
    if (playLayer && !ShadowSimulator::get().isAttachedTo(playLayer)) {
        AssistLog::debug("resetAttempt: shadow missing — re-attach");
        ShadowSimulator::get().attach(playLayer);
    }
    // Death may have cancelled an in-flight trigger bake — resume if still empty.
    TriggerWorldBaker::get().resumeIfEmpty(playLayer);
}

void AssistLifecycle::onLevelInit(AssistController& controller, PlayLayer* playLayer) {
    controller.m_userHolding[0] = controller.m_userHolding[1] = false;
    SelfBakeRecorder::get().onLevelInit(playLayer);
    controller.resetAttempt(playLayer);
    ShadowSimulator::get().attach(playLayer);
    StatusOverlay::attach(playLayer);
    TrajectoryVisualizer::get().setEnabled(StageConfig::showTrajectory());
    TrajectoryVisualizer::get().attach(playLayer);
    BakePathVisualizer::get().setEnabled(StageConfig::showBakePath());
    BakePathVisualizer::get().attach(playLayer);
    if (playLayer && playLayer->m_level) {
        LevelProfile::get().loadForLevel(playLayer->m_level);
        BakeMap::get().loadForLevel(playLayer->m_level);
        TriggerWorldBaker::get().loadOrBuild(playLayer);
        // Movers / TW tracks → dynamic-labelled TrustRate floor (G7b).
        bool const dynamicLabelled = TriggerWorldStore::get().hasTracks();
        AssistRealMetrics::get().setLevelKey(LevelProfile::get().key(), dynamicLabelled);
    }
    controller.updateOverlay();
}

void AssistLifecycle::onLevelQuit(AssistController& controller) {
    // Tear down UI/sim WHILE PlayLayer is still alive. Leaving nodes in
    // objectLayer for setNextScene → ~GameObject / CCObject::release AVs.
    auto* playLayer = PlayLayer::get();
    // Dev traces must flush BEFORE ghost evacuate / detach (partial OK).
    DevCycleRecorder::get().flushQuit();
    ShadowSimulator::get().abortEval();
    TriggerWorldBaker::get().cancel();
    SelfBakeRecorder::get().cancelRefine();
    SelfBakeRecorder::get().flushMergeOnly(playLayer, "quit");
    SelfBakeRecorder::get().onLevelQuit();
    SaveFeedback::get().reset();
    ClickIntentGuard::get().reset();
    // Prefer live detach (removeFromParent) while PlayLayer exists.
    bool const layerAlive = playLayer != nullptr;
    SaveFrameCounter::get().detach(!layerAlive);
    TrajectoryVisualizer::get().detach(!layerAlive);
    BakePathVisualizer::get().detach(!layerAlive);
    StatusOverlay::detach(!layerAlive);

    if (playLayer) {
        ShadowSimulator::get().evacuateGhosts(playLayer);
    }
    // removeFromParentAndCleanup now — never leave ghosts for setNextScene.
    ShadowSimulator::get().detach(false);

    // Persist then clear. Skip PerfTrace sync flush here — large logs were
    // hitching quits; pending lines flush on next enable / natural batch.
    LevelProfile::get().save();
    BakeMap::get().save();
    {
        LagProbe::Scope const state("eb.triggerBake.save");
        TriggerWorldStore::get().save();
    }
    LevelProfile::get().clear();
    TriggerWorldStore::get().clear();
    SimTrust::get().onLevelQuit();
    GhostParityProbe::get().onLevelQuit();
    controller.m_dafWatch = false;
    // Keep AssistRealMetrics session across levels (corpus report); do not resetSession.

    // Quiet reset — overlay already detached
    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseTick = 0;
    controller.m_attemptTick = 0;
    controller.m_inRealUpdate = false;
    controller.m_subDtSeen = 0.f;
    controller.m_modeKnown = false;
    controller.m_startSnapshotTaken = false;
    controller.m_startSnapshotSlot = -1;
    controller.m_lastBakeHint.clear();
    controller.m_log.clear();
    controller.m_clickTimes.clear();
    controller.m_cpsLocked = false;
    controller.m_userHolding[0] = controller.m_userHolding[1] = false;
}

geode::Result<> AssistLifecycle::ensureStartSnapshot(AssistController& controller,
                                                     PlayLayer* layer) {
    if (!layer)
        return geode::Err("No layer");
    // Prefer reserved start slot captured at attempt begin. Only (re)capture
    // while still near frame 0 — never overwrite with mid-run pose at bake time.
    if (!controller.m_startSnapshotTaken || controller.m_startSnapshotSlot < 0) {
        if (layer->m_player1 && controller.m_attemptTick <= 5) {
            controller.m_startSnapshotSlot =
                ShadowSimulator::get().captureStartSnapshot(layer->m_player1);
            controller.m_startSnapshotTaken = controller.m_startSnapshotSlot >= 0;
        }
    }
    if (!controller.m_startSnapshotTaken ||
        controller.m_startSnapshotSlot != ShadowSimulator::START_SNAPSHOT_SLOT) {
        return geode::Err("Reset the level once (start snapshot), then bake");
    }
    if (!ShadowSimulator::get().snapshotSlot(controller.m_startSnapshotSlot)) {
        return geode::Err("Start snapshot missing — reset the level, then bake");
    }
    return geode::Ok();
}

geode::Result<int> AssistLifecycle::runBakeMap(AssistController& controller, PlayLayer* layer,
                                               bool bruteForce) {
    auto snap = controller.ensureStartSnapshot(layer);
    if (snap.isErr())
        return geode::Err(snap.unwrapErr());
    auto method = bruteForce ? BakeMethod::BruteForce : BakeMethod::Replay;
    return BakeMap::get().bake(layer, controller.m_startSnapshotSlot, StageConfig::bakeRadius(),
                               method);
}

void AssistLifecycle::resetLevelProfile(AssistController& controller) {
    LevelProfile::get().resetCurrent();
    controller.updateOverlay();
}

void AssistLifecycle::beginRealUpdate(AssistController& controller) {
    // True only while PlayerObject::update runs for real P1
    // (see GameLayerHooks). CBF / mid-step clicks set midStep=true.
    controller.m_inRealUpdate = true;
    controller.m_subDtSeen = 0.f;
}

void AssistLifecycle::endRealUpdate(AssistController& controller) {
    controller.m_inRealUpdate = false;
}

void AssistLifecycle::onSubStep(AssistController& controller, PlayerObject* player,
                                float deltaTime) {
    if (!controller.m_inRealUpdate || deltaTime <= 0.f)
        return;
    auto* playLayer = PlayLayer::get();
    if (!playLayer || player != playLayer->m_player1)
        return;
    controller.m_subDtSeen += AssistClock::gdStepToSeconds(deltaTime);
}
