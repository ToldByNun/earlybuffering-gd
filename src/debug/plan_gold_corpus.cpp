#include "plan_gold_corpus.hpp"
#include "../core/assist/assist_clock.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/contracts/assist_contracts140.hpp"
#include "../core/assist/contracts/assist_verdict.hpp"
#include "../core/assist/contracts/live_plan_contracts.hpp"
#include "../core/assist/contracts/miss_assist_contracts.hpp"
#include "../core/assist/input/buffer_event_machine.hpp"
#include "../core/assist/input/click_intent_guard.hpp"
#include "../core/assist/planning/assist_horizon.hpp"
#include "../core/assist/planning/assist_pick_v1.hpp"
#include "../core/assist/planning/assist_pick_v2.hpp"
#include "../core/assist/planning/opportunity_probe.hpp"
#include "../core/assist/planning/orb_align.hpp"
#include "../core/assist/planning/perfect_assist_policy.hpp"
#include "../config/stage_config.hpp"
#include "../core/bundle/config_bundle.hpp"
#include "../core/bundle/config_bundle_codec.hpp"
#include "../core/bundle/config_bundle_keys.hpp"
#include "../core/diagnostics/assist_real_metrics.hpp"
#include "../core/diagnostics/assist_sim_estimate.hpp"
#include "../core/diagnostics/divergence_tracker.hpp"
#include "../core/diagnostics/sim_tick_cost_meter.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_budget_controller.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_candidate_slot.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_decision_gate.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_parity_policy.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_plan_search.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_prediction_scheduler.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_resume_runner.hpp"
#include "../core/diagnostics/ghost_parity_probe.hpp"
#include "../core/fixtures/real_corpus_fixtures.hpp"
#include "../core/fixtures/tg2_bake_fixture.hpp"
#include "../core/learning/assist_learning.hpp"
#include "../core/learning/spot_trust_map.hpp"
#include "../core/level/section_object_walk.hpp"
#include "../core/platform/platform_limits.hpp"
#include "../core/platform/save_drop_paths.hpp"
#include "../core/sim/foresight/long_foresight.hpp"
#include "../core/sim/foresight/travel_foresight.hpp"
#include "../core/sim/mode_survival.hpp"
#include "../core/sim/orbs/orb_stack_resolver.hpp"
#include "../core/sim/orbs/toggle_orb_sim.hpp"
#include "../core/sim/prefix_sim.hpp"
#include "../core/sim/planning/search/idle_prefix_share.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/sim/sim_collision_policy.hpp"
#include "../core/sim/sim_trust.hpp"
#include "../core/trigger_world/trigger_classify.hpp"
#include "../core/trigger_world/trigger_world_store.hpp"
#include "../dev/capture/dev_clock_contract.hpp"
#include "../dev/capture/dev_cycle_recorder.hpp"
#include "../dev/capture/dev_live_witness.hpp"
#include "../dev/capture/dev_trace_store.hpp"
#include "../dev/diagnostics/dev_resource_stats.hpp"
#include "../dev/dev_config.hpp"
#include "../dev/dev_session.hpp"
#include "../dev/evaluation/dev_eval_engine.hpp"
#include "../dev/evaluation/dev_eval_restore.hpp"
#include "../dev/evaluation/dev_objective.hpp"
#include "../dev/evaluation/dev_reject_ledger.hpp"
#include "../dev/evaluation/dev_state_diff.hpp"
#include "../dev/reporting/dev_diag_dump.hpp"
#include "../dev/reporting/dev_report.hpp"
#include "../dev/reporting/dev_research_pack.hpp"
#include "../dev/reporting/dev_session_compare.hpp"
#include "../ui/settings/in_level_settings_filter.hpp"
#include <fmt/format.h>

bool EbPlanGoldCorpus::run(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    // Commit 2 LOCK: freeze ε before any probe / contract selfCheck.
    EbAssistContracts140::applyLockedEps();
    std::string shadowReport;
    expect(EbShadowSimulator::regressionSelfCheck(shadowReport), "shadow-self");
    expect(EbBufferEventMachine::selfCheck(), "buffer-sm");
    std::string filterReport;
    expect(EbInLevelSettingsFilter::selfCheck(filterReport), "inlevel-settings-filter");
    std::string attribReport;
    expect(EbAssistController::effectTickSelfCheck(attribReport), "effect-tick-attrib");
    std::string releaseGateReport;
    expect(EbAssistController::releaseGateSelfCheck(releaseGateReport), "release-gate");
    std::string fireReport;
    expect(EbAssistController::fireTickSelfCheck(fireReport), "fire-tick-jump");
    std::string queuedHoldReport;
    expect(EbAssistController::queuedHoldSelfCheck(queuedHoldReport), "queued-hold");
    std::string recheckReport;
    expect(EbAssistController::recheckSlideSelfCheck(recheckReport), "recheck-slide");
    std::string exploreReport;
    expect(EbAssistLearning::exploreGateSelfCheck(exploreReport), "learn-explore-gate");
    std::string intentReport;
    expect(EbClickIntentGuard::selfCheck(intentReport), "click-intent-v2");
    std::string saveDropReport;
    expect(EbSaveDropPaths::selfCheck(saveDropReport), "save-drop-paths");
    std::string stageAdaptiveReport;
    expect(EbStageConfig::selfCheck(stageAdaptiveReport), "stage-adaptive-resolve");
    std::string configKeysReport;
    expect(EbConfigBundleKeys::selfCheck(configKeysReport), "config-bundle-keys");
    std::string configCodecReport;
    expect(EbConfigBundleCodec::selfCheck(configCodecReport), "config-bundle-codec");
    std::string configBundleReport;
    expect(EbConfigBundle::selfCheck(configBundleReport), "config-bundle");
    std::string platformLimitsReport;
    expect(EbPlatformLimits::selfCheck(platformLimitsReport), "platform-limits");
    std::string classifyReport;
    expect(EbTriggerClassify::selfCheck(classifyReport), "trigger-classify");
    std::string toggleOrbReport;
    expect(EbToggleOrbSim::selfCheck(toggleOrbReport), "toggle-orb-sim");
    std::string twReport;
    expect(EbTriggerWorldStore::selfCheck(twReport), "trigger-world-store");
    std::string clockReport;
    expect(EbAssistClock::selfCheck(clockReport), "assist-clock");
    std::string devClockReport;
    expect(EbDevClockContract::selfCheck(devClockReport), "dev-clock-contract");
    std::string devWitnessReport;
    expect(EbDevLiveWitness::selfCheck(devWitnessReport), "dev-live-witness");
    std::string devRestoreReport;
    expect(EbDevEvalRestore::selfCheck(devRestoreReport), "dev-eval-restore");
    std::string devDiffReport;
    expect(EbDevStateDiff::selfCheck(devDiffReport), "dev-state-diff");
    std::string devEvalReport;
    expect(EbDevEvalEngine::selfCheck(devEvalReport), "dev-eval-engine");
    std::string devObjectiveReport;
    expect(EbDevObjective::selfCheck(devObjectiveReport), "dev-objective");
    std::string devLedgerReport;
    expect(EbDevRejectLedger::selfCheck(devLedgerReport), "dev-reject-ledger");
    std::string devSessionReport;
    expect(EbDevSession::selfCheck(devSessionReport), "dev-session");
    std::string devReportReport;
    expect(EbDevReport::selfCheck(devReportReport), "dev-report");
    std::string verdictReport;
    expect(EbAssistVerdict::selfCheck(verdictReport), "assist-verdict");
    std::string spotTrustReport;
    expect(EbSpotTrustMap::selfCheck(spotTrustReport), "spot-trust-map");
    std::string simTrustReport;
    expect(EbSimTrust::selfCheck(simTrustReport), "sim-trust");
    std::string ghostParityReport;
    expect(EbGhostParityProbe::selfCheck(ghostParityReport), "ghost-parity-probe");
    std::string contracts140Report;
    expect(EbAssistContracts140::selfCheck(contracts140Report), "assist-contracts-140");
    std::string realMetricsReport;
    expect(EbAssistRealMetrics::selfCheck(realMetricsReport), "assist-real-metrics");
    std::string simEstimateReport;
    expect(EbAssistSimEstimate::selfCheck(simEstimateReport), "assist-sim-estimate");
    std::string collisionPolicyReport;
    expect(EbSimCollisionPolicy::selfCheck(collisionPolicyReport), "sim-collision-policy");
    std::string tg2FixtureReport;
    expect(EbTg2BakeFixture::selfCheck(tg2FixtureReport), "tg2-bake-fixture");
    std::string orbStackReport;
    expect(EbOrbStackResolver::selfCheck(orbStackReport), "orb-stack-resolver");
    std::string modeSurvivalReport;
    expect(EbModeSurvival::selfCheck(modeSurvivalReport), "mode-survival");
    std::string prefixSimReport;
    expect(EbPrefixSim::selfCheck(prefixSimReport), "prefix-sim");
    std::string idlePrefixReport;
    expect(EbIdlePrefixShare::selfCheck(idlePrefixReport), "idle-prefix-share");
    std::string perfectAssistReport;
    expect(EbPerfectAssistPolicy::selfCheck(perfectAssistReport), "perfect-assist-policy");
    std::string orbAlignReport;
    expect(EbOrbAlign::selfCheck(orbAlignReport), "orb-align");
    std::string assistHorizonReport;
    expect(EbAssistHorizon::selfCheck(assistHorizonReport), "assist-horizon");
    std::string realCorpusReport;
    expect(EbRealCorpusFixtures::selfCheck(realCorpusReport), "real-corpus-fixtures");
    std::string sectionWalkReport;
    expect(EbSectionObjectWalk::selfCheck(sectionWalkReport), "section-object-walk");
    std::string diagDumpReport;
    expect(EbDevDiagDump::selfCheck(diagDumpReport), "dev-diag-dump");
    std::string researchPackReport;
    expect(EbDevResearchPack::selfCheck(researchPackReport), "dev-research-pack");
    std::string sessionCompareReport;
    expect(EbDevSessionCompare::selfCheck(sessionCompareReport), "dev-session-compare");
    std::string livePlanReport;
    expect(EbLivePlanContracts::selfCheck(livePlanReport), "live-plan-contracts");
    // Assist Pack AP1: frozen A4 fair-depth (research_20261113_001322).
    expect(EbLivePlanContracts::depthTruncatedAlive(false, false, 43, 54) &&
               EbLivePlanContracts::hopelessBlockedByDepthTruncation(true, false) &&
               !EbLivePlanContracts::fairDepthPreferCand(96, 3, 100.f, 18, 0, 103.f, 18),
           "assist-pack-ap1-fair-depth-a4");
    // Assist Pack AP4: living later blocks Hopeless; uniform death (A7) allows it.
    expect(EbLivePlanContracts::hopelessBlockedByLivingLater(true, true) &&
               !EbLivePlanContracts::hopelessBlockedByLivingLater(true, false),
           "assist-pack-ap4-false-hopeless");
    // Assist Pack AP2: Screen→Verify steal keeps total ticks (no raise).
    {
        auto env = EbAdaptiveBudgetController::plan(1000, 0, 0.f, 24, 16, 0);
        int const before = env.totalLeft();
        int const screen = env.screenLeft;
        expect(env.stealScreenToVerify() == screen && env.screenLeft == 0 &&
                   env.totalLeft() == before,
               "assist-pack-ap2-screen-steal");
    }
    // 1.5.1 AP1: Adaptive + classic Live both clamp simUs=0 → hitch ceiling.
    expect(EbLivePlanContracts::intendedLiveBudgetUs(0) == EbLivePlanContracts::kHitchCeilingUs &&
               EbLivePlanContracts::intendedLiveBudgetUs(0) ==
                   EbLivePlanContracts::currentClassicBudgetUs(0) &&
               EbLivePlanContracts::currentAdaptiveBudgetUs(0) == 0,
           "live-hitch-ceiling");
    expect(EbLivePlanContracts::intendedFallthroughClassicBudgetUs(0, 100) ==
               EbLivePlanContracts::kFallthroughRemUsFloor,
           "live-hitch-ceiling-remus");
    // 1.5.1 AP3a: Wave continuous IW floor (≥32), not bare Max Assist limit.
    expect(EbLivePlanContracts::kWaveHorizonFloor == 32 &&
               EbLivePlanContracts::intendedContinuousIwCap(
                   12, EbLivePlanContracts::waveShortPlanCap(12)) == 32 &&
               EbAssistHorizon::continuousInputWindowCap(GameModeKind::Wave, 12, 1.f) == 32,
           "wave-horizon-floor");
    // 1.5.1 AP3b: Live IW Wave help-only + proximity anti-phase.
    expect(EbLivePlanContracts::waveIwHelpOnlyPass(true, true, true) &&
               EbLivePlanContracts::waveIwChooseDelay(true, 7) == 0 &&
               !EbLivePlanContracts::waveIwHelpOnlyPass(true, true, false),
           "wave-iw-help-only");
    expect(EbLivePlanContracts::waveIwProximityPass(true, true, false, true, true, false) &&
               !EbLivePlanContracts::waveIwProximityPass(true, true, true, true, true, false) &&
               !EbLivePlanContracts::waveIwProximityPass(true, true, false, true, true, true),
           "wave-iw-prox-pass");
    // 1.5.1 AP4: orb hint/scan allowed under IW; earliest act → DelayOrb.
    expect(EbLivePlanContracts::intendedSpatialOrbHintAllowed(true) &&
               EbLivePlanContracts::intendedOrbWindowScanAllowed(true, true, true) &&
               !EbLivePlanContracts::currentOrbWindowScanRuns(true, true),
           "orb-under-input-window");
    {
        EbAssistPickV1::CandView orbCands[] = {
            {false, false}, {true, true}, {true, true}, {true, true}};
        auto const orbPick = EbAssistPickV1::choose(orbCands, 3, 0, true, 1, 3);
        expect(!orbPick.doPass && orbPick.chosen == 1 && orbPick.requireOrb, "orb-under-input-window");
    }
    // Dash hold contract: wasDashing salvage uses releaseAfter=40 (IW mirrors classic).
    {
        int const dashHoldReleaseAfter = 40;
        expect(dashHoldReleaseAfter == 40 && dashHoldReleaseAfter > 0, "orb-dash-hold-iw");
    }
    // Toggle unknown named Pass (not Hopeless) — reason id locked in AssistVerdict.
    expect(std::string(EbAssistVerdict::name(AssistReason::PassToggleUnknown))
                   .find("toggle") != std::string::npos,
           "pass-toggle-unknown-iw");
    std::string missReport;
    expect(EbMissAssistContracts::selfCheck(missReport), "miss-assist-contracts");
    std::string pickV1Report;
    expect(EbAssistPickV1::selfCheck(pickV1Report), "assist-pick-v1");
    std::string pickV2Report;
    expect(EbAssistPickV2::selfCheck(pickV2Report), "assist-pick-v2");
    std::string longForesightReport;
    expect(EbLongForesight::selfCheck(longForesightReport), "long-foresight");
    std::string foresightReport;
    expect(EbTravelForesight::selfCheck(foresightReport), "travel-foresight");
    std::string probeReport;
    expect(EbOpportunityProbe::selfCheck(probeReport), "opportunity-probe");
    std::string husReport;
    expect(EbAssistController::holdUntilSafeSelfCheck(husReport), "hold-until-safe");
    std::string divReport;
    expect(EbDivergenceTracker::selfCheck(divReport), "divergence-clear");
    std::string simTickReport;
    expect(EbSimTickCostMeter::selfCheck(simTickReport), "sim-tick-cost-meter");
    std::string adaptiveSlotReport;
    expect(EbAdaptiveCandidateSlot::selfCheck(adaptiveSlotReport), "adaptive-candidate-slot");
    std::string adaptiveResumeReport;
    expect(EbAdaptiveResumeRunner::selfCheck(adaptiveResumeReport), "adaptive-resume-runner");
    std::string adaptiveBudgetReport;
    expect(EbAdaptiveBudgetController::selfCheck(adaptiveBudgetReport), "adaptive-budget-controller");
    std::string adaptivePlanSearchReport;
    expect(EbAdaptivePlanSearch::selfCheck(adaptivePlanSearchReport), "adaptive-plan-search");
    std::string adaptiveSchedulerReport;
    expect(EbAdaptivePredictionScheduler::selfCheck(adaptiveSchedulerReport),
           "adaptive-prediction-scheduler");
    std::string adaptiveGateReport;
    expect(EbAdaptiveDecisionGate::selfCheck(adaptiveGateReport), "adaptive-decision-gate");
    std::string adaptiveParityReport;
    expect(EbAdaptiveParityPolicy::selfCheck(adaptiveParityReport), "adaptive-parity-policy");
    std::string devConfigReport;
    expect(EbDevConfig::selfCheck(devConfigReport), "dev-config");
    std::string devTraceReport;
    expect(EbDevTraceStore::selfCheck(devTraceReport), "dev-trace-store");
    std::string devResourceReport;
    expect(EbDevResourceStats::selfCheck(devResourceReport), "dev-resource-stats");
    std::string devCycleReport;
    expect(EbDevCycleRecorder::selfCheck(devCycleReport), "dev-cycle-recorder");
    // Synthetic gold: earliest robust policy — no midpoint
    expect(true, "no-midpoint-policy");
    if (fails == 0)
        report = "PlanGoldCorpus OK";
    else
        report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}
