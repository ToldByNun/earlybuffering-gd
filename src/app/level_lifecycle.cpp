#include "level_lifecycle.hpp"
#include "../config/assist_policy_settings.hpp"
#include "../config/stage_config.hpp"
#include "../core/assist/assist_clock.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/input/click_intent_guard.hpp"
#include "../core/assist/runtime/assist_lifecycle.hpp"
#include "../core/bake/bake_map.hpp"
#include "../core/bake/bake_verifier.hpp"
#include "../core/bake/hybrid_verify_cache.hpp"
#include "../core/bake/self_bake_recorder.hpp"
#include "../core/diagnostics/assist_real_metrics.hpp"
#include "../core/diagnostics/divergence_tracker.hpp"
#include "../core/diagnostics/ghost_parity_probe.hpp"
#include "../core/learning/hybrid_learning.hpp"
#include "../core/learning/level_profile.hpp"
#include "../core/learning/neural_autoplay.hpp"
#include "../core/learning/neural_learning.hpp"
#include "../core/level/level_content_key.hpp"
#include "../core/real_assist/real_assist_runner.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/sim/sim_trust.hpp"
#include "../core/sim/world/shadow_world_capture.hpp"
#include "../core/trigger_world/trigger_world_baker.hpp"
#include "../core/trigger_world/trigger_world_store.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/interaction_journal.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../dev/capture/dev_cycle_recorder.hpp"
#include "../ui/feedback/lag_warning_toast.hpp"
#include "../ui/feedback/save_feedback.hpp"
#include "../ui/feedback/save_frame_counter.hpp"
#include "../ui/overlays/status_overlay.hpp"
#include "../ui/visualizers/ai_visualizer.hpp"
#include "../ui/visualizers/bake_path_visualizer.hpp"
#include "../ui/visualizers/straightfly_visualizer.hpp"
#include "../ui/visualizers/trajectory_visualizer.hpp"
#include "../ui/visualizers/wave_pattern_visualizer.hpp"

EbLevelLifecycle::EnterLoadPhase EbLevelLifecycle::s_enterLoadPhase =
    EbLevelLifecycle::EnterLoadPhase::Idle;

EbTransitionPhases::EbTransitionPhases(char const* transition)
    : m_transition(transition), m_started(std::chrono::steady_clock::now()) {}

void EbTransitionPhases::mark(char const* phase) {
    auto const now = std::chrono::steady_clock::now();
    auto const elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - m_started).count();
    m_started = now;
    if (elapsed >= 1000)
        EbAssistLog::info(
            fmt::format("TRANSITION {} phase={} us={}", m_transition, phase, elapsed));
}

void EbLevelLifecycle::onCheckpointLoad(EbAssistController& controller, PlayLayer* playLayer) {
    EbInteractionJournal::get().note("checkpoint.load");
    // Do not flush/refine here — idle drain handles the buffer after respawn.
    // Trigger world: keep store + in-flight baker; only reset abs attempt tick.
    EbHybridLearning::get().boundary(false);
    EbNeuralLearning::get().boundary(false);
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
    EbInteractionJournal::get().note("level.complete",
                                     fmt::format("tick={}", controller.m_attemptTick));
    EbDevCycleRecorder::get().sealAttemptWithOutcomes("complete", true, controller.m_attemptTick);
    EbSelfBakeRecorder::get().flushMergeOnly(playLayer, "complete");
    EbLevelProfile::get().save();
    EbBakeMap::get().save();
    {
        EbLagProbe::Scope const state("eb.triggerBake.save");
        EbTriggerWorldStore::get().save();
    }
    float percent = 100.f;
    if (playLayer && playLayer->m_level)
        percent = static_cast<float>(playLayer->m_level->m_normalPercent);
    EbRealAssistRunner::get().onAttemptEnd(playLayer, true, percent);
}

void EbLevelLifecycle::resetAttempt(EbAssistController& controller, PlayLayer* playLayer) {
    EbInteractionJournal::get().note("attempt.reset",
                                     fmt::format("tickWas={}", controller.m_attemptTick));
    // Self-bake stays buffered — idle drain merges after we're alive again.
    EbLagProbe::Scope const episodeScope("eb.resetAttempt");
    if (EbRealAssistRunner::get().active() && controller.attemptTick() > 0 && playLayer) {
        float const pct = playLayer->getCurrentPercent();
        EbRealAssistRunner::get().onAttemptEnd(playLayer, false, pct);
    }
    {
        EbLagProbe::Scope const state("eb.flushSegment");
        EbSelfBakeRecorder::get().flushSegment(playLayer, "reset", false);
    }
    // If death already sealed the attempt, open the next; else seal as reset.
    EbHybridLearning::get().boundary(true);
    EbNeuralLearning::get().boundary(true);
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
    controller.m_flightIntent[0].reset();
    controller.m_flightIntent[1].reset();
    controller.m_wavePattern[0].reset();
    controller.m_wavePattern[1].reset();
    controller.m_flightCorrectionObserved[0] = controller.m_flightCorrectionObserved[1] = false;
    controller.m_shipLineHasAnchor[0] = controller.m_shipLineHasAnchor[1] = false;
    controller.m_shipLineOffSeconds[0] = controller.m_shipLineOffSeconds[1] = 0.;
    controller.m_waveLineHasAnchor[0] = controller.m_waveLineHasAnchor[1] = false;
    controller.m_waveLineOffSeconds[0] = controller.m_waveLineOffSeconds[1] = 0.;
    controller.m_lastShipLineDelay = 0;
    controller.m_lastShipLineTick = -1;
    controller.m_lastWavePatternDelay = 0;
    controller.m_lastWavePatternTick = -1;
    controller.m_flightMeasurement[0].reset();
    controller.m_flightMeasurement[1].reset();
    controller.m_inputReconciler.reset();
    // Death often skips the matching UP — stuck true blocks rawEdge / Wave+Ship lock.
    controller.m_userHolding[0] = controller.m_userHolding[1] = false;
    EbNeuralAutoplay::get().onAttemptReset();
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
    // Retry can rebuild objectLayer and orphan draw nodes; re-arm viz from settings.
    if (playLayer) {
        EbStraightflyVisualizer::get().setEnabled(EbStageConfig::showStraightfly());
        if (EbStageConfig::showStraightfly())
            EbStraightflyVisualizer::get().attach(playLayer);
        EbWavePatternVisualizer::get().setEnabled(EbStageConfig::showWavePattern());
        if (EbStageConfig::showWavePattern())
            EbWavePatternVisualizer::get().attach(playLayer);
        EbAiVisualizer::get().setEnabled(EbStageConfig::showAi());
        if (EbStageConfig::showAi())
            EbAiVisualizer::get().attach(playLayer);
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
    // Respawn can replace the object array or reattach the ghost. Refresh the
    // structural index before the first input, once level objects are available.
    // Skip during enter — onLevelInit rebuilds after bake-load when objects are ready.
    if (playLayer && playLayer->m_objects && playLayer->m_objects->count() &&
        !EbLevelContentKey::SetupScope::active())
        EbShadowWorldCapture::refreshObjectIndex(EbShadowSimulator::get(), playLayer);
    // Death may have cancelled an in-flight trigger bake — resume if still empty.
    EbTriggerWorldBaker::get().resumeIfEmpty(playLayer);
}

void EbLevelLifecycle::onLevelInit(EbAssistController& controller, PlayLayer* playLayer) {
    EbTransitionPhases phases("enter");
    std::string enterDetail;
    if (playLayer && playLayer->m_level) {
        enterDetail =
            fmt::format("key={} objects~{}", EbLevelContentKey::fullKey(playLayer->m_level),
                        playLayer->m_objects ? playLayer->m_objects->count() : 0);
    }
    EbInteractionJournal::Scope const enterScope("level.enter", std::move(enterDetail));
    EbLevelContentKey::SetupScope const keyScope(playLayer ? playLayer->m_level : nullptr);
    phases.mark("content-key");
    controller.m_flightIntent[0].reset();
    controller.m_flightIntent[1].reset();
    controller.m_wavePattern[0].reset();
    controller.m_wavePattern[1].reset();
    controller.m_flightCorrectionObserved[0] = controller.m_flightCorrectionObserved[1] = false;
    controller.m_shipLineHasAnchor[0] = controller.m_shipLineHasAnchor[1] = false;
    controller.m_shipLineOffSeconds[0] = controller.m_shipLineOffSeconds[1] = 0.;
    controller.m_waveLineHasAnchor[0] = controller.m_waveLineHasAnchor[1] = false;
    controller.m_waveLineOffSeconds[0] = controller.m_waveLineOffSeconds[1] = 0.;
    controller.m_lastShipLineDelay = 0;
    controller.m_lastShipLineTick = -1;
    controller.m_lastWavePatternDelay = 0;
    controller.m_lastWavePatternTick = -1;
    controller.m_flightMeasurement[0].reset();
    controller.m_flightMeasurement[1].reset();
    controller.m_inputReconciler.reset();
    controller.m_userHolding[0] = controller.m_userHolding[1] = false;
    EbSelfBakeRecorder::get().onLevelInit(playLayer);
    // Attach once before resetAttempt. resetAttempt re-attaches only if missing;
    // a second unconditional attach here caused detach→HardStreak::reset AVs.
    EbShadowSimulator::get().attach(playLayer);
    phases.mark("ghost-create");
    controller.m_liveHorizon.reset();
    controller.resetAttempt(playLayer);
    phases.mark("attempt-reset-index");
    EbStatusOverlay::attach(playLayer);
    EbLagWarningToast::get().attach(playLayer);
    EbTrajectoryVisualizer::get().setEnabled(EbStageConfig::showTrajectory());
    EbTrajectoryVisualizer::get().attach(playLayer);
    EbStraightflyVisualizer::get().setEnabled(EbStageConfig::showStraightfly());
    EbStraightflyVisualizer::get().attach(playLayer);
    EbWavePatternVisualizer::get().setEnabled(EbStageConfig::showWavePattern());
    EbWavePatternVisualizer::get().attach(playLayer);
    EbAiVisualizer::get().setEnabled(EbStageConfig::showAi());
    EbAiVisualizer::get().attach(playLayer);
    EbBakePathVisualizer::get().setEnabled(EbStageConfig::showBakePath());
    EbBakePathVisualizer::get().attach(playLayer);
    phases.mark("overlays");
    EbAssistPolicySettings::refresh();
    // Disk JSON (profile / bake / TW) + full object index used to hitch enter
    // intermittently — especially TW cache hits parsing multi‑MB files inside
    // PlayLayer::init. Defer until physics ticks after the loading screen.
    if (playLayer && playLayer->m_level) {
        s_enterLoadPhase = EnterLoadPhase::Profile;
        phases.mark("defer-loads");
    } else {
        s_enterLoadPhase = EnterLoadPhase::Idle;
    }
    EbRealAssistRunner::get().onLevelInit(playLayer);
    controller.updateOverlay();
    phases.mark("finish");
}

void EbLevelLifecycle::drainDeferredEnterLoads(PlayLayer* playLayer) {
    if (s_enterLoadPhase == EnterLoadPhase::Idle)
        return;
    if (!playLayer || playLayer != PlayLayer::get() || !playLayer->m_level) {
        s_enterLoadPhase = EnterLoadPhase::Idle;
        return;
    }

    char const* phaseName = "defer.?";
    switch (s_enterLoadPhase) {
    case EnterLoadPhase::Profile:
        phaseName = "defer.profile";
        break;
    case EnterLoadPhase::BakeMap:
        phaseName = "defer.bakeMap";
        break;
    case EnterLoadPhase::TriggerWorld:
        phaseName = "defer.triggerWorld";
        break;
    case EnterLoadPhase::ObjectIndex:
        phaseName = "defer.objectIndex";
        break;
    case EnterLoadPhase::Metrics:
        phaseName = "defer.metrics";
        break;
    case EnterLoadPhase::Idle:
    default:
        break;
    }
    EbInteractionJournal::Scope const deferScope(phaseName);

    auto const wall0 = std::chrono::steady_clock::now();
    // One phase per call — TW JSON parse alone can exceed any slice budget.
    // Prefer a short hitch after the loading screen over blocking PlayLayer::init.
    switch (s_enterLoadPhase) {
    case EnterLoadPhase::Profile:
        EbLevelProfile::get().loadForLevel(playLayer->m_level);
        s_enterLoadPhase = EnterLoadPhase::BakeMap;
        break;
    case EnterLoadPhase::BakeMap:
        EbBakeMap::get().loadForLevel(playLayer->m_level);
        s_enterLoadPhase = EnterLoadPhase::TriggerWorld;
        break;
    case EnterLoadPhase::TriggerWorld:
        EbTriggerWorldBaker::get().loadOrBuild(playLayer);
        s_enterLoadPhase = EnterLoadPhase::ObjectIndex;
        break;
    case EnterLoadPhase::ObjectIndex:
        EbShadowWorldCapture::refreshObjectIndex(EbShadowSimulator::get(), playLayer);
        s_enterLoadPhase = EnterLoadPhase::Metrics;
        break;
    case EnterLoadPhase::Metrics: {
        bool const dynamicLabelled = EbTriggerWorldStore::get().hasTracks();
        EbAssistRealMetrics::get().setLevelKey(EbLevelProfile::get().key(), dynamicLabelled);
        s_enterLoadPhase = EnterLoadPhase::Idle;
        break;
    }
    case EnterLoadPhase::Idle:
    default:
        s_enterLoadPhase = EnterLoadPhase::Idle;
        break;
    }

    auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - wall0)
                             .count();
    if (elapsed >= 1000) {
        EbAssistLog::info(fmt::format("TRANSITION deferred-enter us={} pending={}", elapsed,
                                      s_enterLoadPhase != EnterLoadPhase::Idle ? 1 : 0));
    }
}

void EbLevelLifecycle::onLevelQuit(EbAssistController& controller, PlayLayer* playLayer) {
    EbTransitionPhases phases("quit");
    auto const quitWall0 = std::chrono::steady_clock::now();
    EbInteractionJournal::get().note(
        "level.quit.begin",
        fmt::format("tick={} tw={} twEmptyOk={}", controller.m_attemptTick,
                    EbTriggerWorldStore::get().trackCount(),
                    EbTriggerWorldStore::get().emptyComplete() ? 1 : 0));
    s_enterLoadPhase = EnterLoadPhase::Idle;
    // Tear down UI/sim WHILE PlayLayer is still alive. Leaving nodes in
    // objectLayer for setNextScene → ~GameObject / CCObject::release AVs.
    // Dev traces must flush BEFORE ghost evacuate / detach (partial OK).
    {
        EbInteractionJournal::Scope const state("quit.traceFlush");
        EbDevCycleRecorder::get().flushQuit();
    }
    phases.mark("trace-snapshot");
    EbShadowSimulator::get().abortEval();
    EbTriggerWorldBaker::get().cancel();
    EbSelfBakeRecorder::get().cancelRefine();
    {
        EbInteractionJournal::Scope const state("quit.selfBakeFlush");
        EbSelfBakeRecorder::get().flushMergeOnly(playLayer, "quit");
        EbSelfBakeRecorder::get().onLevelQuit();
    }
    phases.mark("bake-flush");
    EbSaveFeedback::get().reset();
    EbClickIntentGuard::get().reset();
    // Prefer live detach (removeFromParent) while PlayLayer exists.
    bool const layerAlive = playLayer != nullptr;
    EbSaveFrameCounter::get().detach(!layerAlive);
    EbLagWarningToast::get().detach(!layerAlive);
    EbTrajectoryVisualizer::get().detach(!layerAlive);
    EbStraightflyVisualizer::get().detach(!layerAlive);
    EbWavePatternVisualizer::get().detach(!layerAlive);
    EbAiVisualizer::get().detach(!layerAlive);
    EbBakePathVisualizer::get().detach(!layerAlive);
    EbStatusOverlay::detach(!layerAlive);
    phases.mark("overlays");

    if (playLayer) {
        EbInteractionJournal::Scope const state("quit.evacuateGhosts");
        EbShadowSimulator::get().evacuateGhosts(playLayer);
    }
    phases.mark("ghost-evacuate");
    // removeFromParentAndCleanup now — never leave ghosts for setNextScene.
    {
        EbInteractionJournal::Scope const state("quit.detachGhosts");
        EbShadowSimulator::get().detach(false);
    }
    phases.mark("ghost-destroy");

    // Persist then clear. Skip PerfTrace sync flush here — large logs were
    // hitching quits; pending lines flush on next enable / natural batch.
    {
        EbInteractionJournal::Scope const state("quit.profileSave");
        EbLevelProfile::get().save();
    }
    phases.mark("profile-save");
    {
        EbInteractionJournal::Scope const state("quit.bakeMapSave");
        EbBakeMap::get().save();
    }
    // save() is a no-op when !dirty — empty-complete sentinel writes once.
    {
        EbLagProbe::Scope const state("eb.triggerBake.save");
        EbInteractionJournal::Scope const journal("quit.triggerWorldSave");
        EbTriggerWorldStore::get().save();
    }
    EbLevelProfile::get().clear();
    EbAssistPolicySettings::refresh();
    EbTriggerWorldStore::get().clear();
    phases.mark("bake-save-clear");
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
    controller.m_flightIntent[0].reset();
    controller.m_flightIntent[1].reset();
    controller.m_wavePattern[0].reset();
    controller.m_wavePattern[1].reset();
    controller.m_flightCorrectionObserved[0] = controller.m_flightCorrectionObserved[1] = false;
    controller.m_shipLineHasAnchor[0] = controller.m_shipLineHasAnchor[1] = false;
    controller.m_shipLineOffSeconds[0] = controller.m_shipLineOffSeconds[1] = 0.;
    controller.m_waveLineHasAnchor[0] = controller.m_waveLineHasAnchor[1] = false;
    controller.m_waveLineOffSeconds[0] = controller.m_waveLineOffSeconds[1] = 0.;
    controller.m_lastShipLineDelay = 0;
    controller.m_lastShipLineTick = -1;
    controller.m_lastWavePatternDelay = 0;
    controller.m_lastWavePatternTick = -1;
    controller.m_flightMeasurement[0].reset();
    controller.m_flightMeasurement[1].reset();
    controller.m_inputReconciler.reset();
    controller.m_clickTimes.clear();
    controller.m_cpsLocked = false;
    controller.m_userHolding[0] = controller.m_userHolding[1] = false;

    double const quitMs =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - quitWall0)
                                .count()) /
        1000.0;
    EbInteractionJournal::get().noteMs("level.quit", quitMs);
    EbInteractionJournal::get().flush();
}
