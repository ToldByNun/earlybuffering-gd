#include "level_lifecycle.hpp"
#include "../core/assist/runtime/assist_lifecycle.hpp"
#include "../config/stage_config.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../dev/capture/dev_cycle_recorder.hpp"
#include "../ui/feedback/lag_warning_toast.hpp"
#include "../ui/feedback/save_feedback.hpp"
#include "../ui/feedback/save_frame_counter.hpp"
#include "../ui/overlays/status_overlay.hpp"
#include "../ui/visualizers/bake_path_visualizer.hpp"
#include "../ui/visualizers/trajectory_visualizer.hpp"
#include "../core/bake/bake_map.hpp"
#include "../core/bake/bake_verifier.hpp"
#include "../core/bake/hybrid_verify_cache.hpp"
#include "../core/bake/self_bake_recorder.hpp"
#include "../core/diagnostics/assist_real_metrics.hpp"
#include "../core/diagnostics/divergence_tracker.hpp"
#include "../core/diagnostics/ghost_parity_probe.hpp"
#include "../core/learning/level_profile.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/sim/sim_trust.hpp"
#include "../core/trigger_world/trigger_world_baker.hpp"
#include "../core/trigger_world/trigger_world_store.hpp"
#include "../core/assist/assist_clock.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/input/click_intent_guard.hpp"

void EbLevelLifecycle::onCheckpointLoad(EbAssistController& controller, PlayLayer* playLayer) {
    // Do not flush/refine here — idle drain handles the buffer after respawn.
    // Trigger world: keep store + in-flight baker; only reset abs attempt tick.
    EbSelfBakeRecorder::get().flushSegment(playLayer, "checkpoint", false);
    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseArmTick = 0;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseDevPressId = 0;
    controller.m_pendingReleaseTick = 0;
    controller.m_outcomePending = false;
    controller.m_attemptTick = 0;
    controller.m_startSnapshotTaken = false;
    controller.m_startSnapshotSlot = -1;
    EbClickIntentGuard::get().reset();
    EbDevCycleRecorder::get().noteCheckpoint(playLayer, 0);
    EbLagProbe::get().mark("triggerWorld.checkpoint", "keep store / no rebuild / attemptTick=0");
}

void EbLevelLifecycle::onLevelComplete(EbAssistController& controller, PlayLayer* playLayer) {
    EbDevCycleRecorder::get().sealAttemptWithOutcomes("complete", true, controller.m_attemptTick);
    EbSelfBakeRecorder::get().flushMergeOnly(playLayer, "complete");
    EbLevelProfile::get().save();
    EbBakeMap::get().save();
    {
        EbLagProbe::Scope const state("eb.triggerBake.save");
        EbTriggerWorldStore::get().save();
    }
}

void EbLevelLifecycle::resetAttempt(EbAssistController& controller, PlayLayer* playLayer) {
    // Self-bake stays buffered — idle drain merges after we're alive again.
    EbLagProbe::Scope const episodeScope("eb.resetAttempt");
    {
        EbLagProbe::Scope const state("eb.flushSegment");
        EbSelfBakeRecorder::get().flushSegment(playLayer, "reset", false);
    }
    // If death already sealed the attempt, open the next; else seal as reset.
    EbDevCycleRecorder::get().onAttemptReset(0);
    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseArmTick = 0;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseDevPressId = 0;
    controller.m_pendingReleaseTick = 0;
    controller.m_assistTouchedRun = false;
    controller.m_savesAttempt = 0;
    EbDivergenceTracker::get().clear();
    EbHybridVerifyCache::get().clearAttempt();
    EbSimTrust::get().resetAttempt();
    EbGhostParityProbe::get().resetAttempt();
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
        EbLagProbe::Scope const state("eb.saveFeedbackReset");
        EbSaveFeedback::get().reset();
    }
    {
        EbLagProbe::Scope const state("eb.clickGuardReset");
        EbClickIntentGuard::get().reset();
    }
    {
        EbLagProbe::Scope const state("eb.syncButtons");
        controller.syncButtonState(playLayer);
    }
    {
        EbLagProbe::Scope const state("eb.updateOverlay");
        controller.updateOverlay();
    }
    // If ghosts were torn down mid-attempt, re-attach so assist works again.
    if (playLayer && !EbShadowSimulator::get().isAttachedTo(playLayer)) {
        EbAssistLog::debug("resetAttempt: shadow missing — re-attach");
        EbShadowSimulator::get().attach(playLayer);
    }
    // Death may have cancelled an in-flight trigger bake — resume if still empty.
    EbTriggerWorldBaker::get().resumeIfEmpty(playLayer);
}

void EbLevelLifecycle::onLevelInit(EbAssistController& controller, PlayLayer* playLayer) {
    controller.m_userHolding[0] = controller.m_userHolding[1] = false;
    EbSelfBakeRecorder::get().onLevelInit(playLayer);
    // Attach once before resetAttempt. resetAttempt re-attaches only if missing;
    // a second unconditional attach here caused detach→HardStreak::reset AVs.
    EbShadowSimulator::get().attach(playLayer);
    controller.resetAttempt(playLayer);
    EbStatusOverlay::attach(playLayer);
    EbLagWarningToast::get().attach(playLayer);
    EbTrajectoryVisualizer::get().setEnabled(EbStageConfig::showTrajectory());
    EbTrajectoryVisualizer::get().attach(playLayer);
    EbBakePathVisualizer::get().setEnabled(EbStageConfig::showBakePath());
    EbBakePathVisualizer::get().attach(playLayer);
    if (playLayer && playLayer->m_level) {
        EbLevelProfile::get().loadForLevel(playLayer->m_level);
        EbBakeMap::get().loadForLevel(playLayer->m_level);
        EbTriggerWorldBaker::get().loadOrBuild(playLayer);
        // Movers / TW tracks → dynamic-labelled TrustRate floor (G7b).
        bool const dynamicLabelled = EbTriggerWorldStore::get().hasTracks();
        EbAssistRealMetrics::get().setLevelKey(EbLevelProfile::get().key(), dynamicLabelled);
    }
    controller.updateOverlay();
}

void EbLevelLifecycle::onLevelQuit(EbAssistController& controller, PlayLayer* playLayer) {
    // Tear down UI/sim WHILE PlayLayer is still alive. Leaving nodes in
    // objectLayer for setNextScene → ~GameObject / CCObject::release AVs.
    // Dev traces must flush BEFORE ghost evacuate / detach (partial OK).
    EbDevCycleRecorder::get().flushQuit();
    EbShadowSimulator::get().abortEval();
    EbTriggerWorldBaker::get().cancel();
    EbSelfBakeRecorder::get().cancelRefine();
    EbSelfBakeRecorder::get().flushMergeOnly(playLayer, "quit");
    EbSelfBakeRecorder::get().onLevelQuit();
    EbSaveFeedback::get().reset();
    EbClickIntentGuard::get().reset();
    // Prefer live detach (removeFromParent) while PlayLayer exists.
    bool const layerAlive = playLayer != nullptr;
    EbSaveFrameCounter::get().detach(!layerAlive);
    EbLagWarningToast::get().detach(!layerAlive);
    EbTrajectoryVisualizer::get().detach(!layerAlive);
    EbBakePathVisualizer::get().detach(!layerAlive);
    EbStatusOverlay::detach(!layerAlive);

    if (playLayer) {
        EbShadowSimulator::get().evacuateGhosts(playLayer);
    }
    // removeFromParentAndCleanup now — never leave ghosts for setNextScene.
    EbShadowSimulator::get().detach(false);

    // Persist then clear. Skip PerfTrace sync flush here — large logs were
    // hitching quits; pending lines flush on next enable / natural batch.
    EbLevelProfile::get().save();
    EbBakeMap::get().save();
    {
        EbLagProbe::Scope const state("eb.triggerBake.save");
        EbTriggerWorldStore::get().save();
    }
    EbLevelProfile::get().clear();
    EbTriggerWorldStore::get().clear();
    EbSimTrust::get().onLevelQuit();
    EbGhostParityProbe::get().onLevelQuit();
    controller.m_dafWatch = false;
    // Keep AssistRealMetrics session across levels (corpus report); do not resetSession.

    // Quiet reset — overlay already detached
    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseDevPressId = 0;
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
