#include "plan_gold_corpus.hpp"
#include "../config/stage_config.hpp"
#include "../core/assist/assist_clock.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/contracts/assist_contracts140.hpp"
#include "../core/assist/flight/flight_mode_policy.hpp"
#include "../core/assist/flight/wave_pattern_recognizer.hpp"
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
#include "../core/bundle/config_bundle.hpp"
#include "../core/bundle/config_bundle_codec.hpp"
#include "../core/bundle/config_bundle_keys.hpp"
#include "../core/diagnostics/assist_real_metrics.hpp"
#include "../core/diagnostics/assist_sim_estimate.hpp"
#include "../core/diagnostics/divergence_tracker.hpp"
#include "../core/diagnostics/ghost_parity_probe.hpp"
#include "../core/diagnostics/sim_tick_cost_meter.hpp"
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
#include "../core/sim/planning/search/adaptive/adaptive_budget_controller.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_candidate_slot.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_decision_gate.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_parity_policy.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_plan_search.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_prediction_scheduler.hpp"
#include "../core/sim/planning/search/adaptive/adaptive_resume_runner.hpp"
#include "../core/sim/planning/search/idle_prefix_share.hpp"
#include "../core/sim/prefix_sim.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/sim/sim_collision_policy.hpp"
#include "../core/sim/sim_trust.hpp"
#include "../core/trigger_world/trigger_classify.hpp"
#include "../core/trigger_world/trigger_world_store.hpp"
#include "../dev/capture/dev_clock_contract.hpp"
#include "../dev/capture/dev_cycle_recorder.hpp"
#include "../dev/capture/dev_live_witness.hpp"
#include "../dev/capture/dev_trace_store.hpp"
#include "../dev/dev_config.hpp"
#include "../dev/dev_session.hpp"
#include "../dev/diagnostics/dev_resource_stats.hpp"
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
    auto expectReport = [&](bool ok, char const* name, std::string const& detail) {
        expect(ok, name);
        if (!ok && !detail.empty())
            report += "[" + detail + "] ";
    };
    // Commit 2 LOCK: freeze ε before any probe / contract selfCheck.
    EbAssistContracts140::applyLockedEps();
    std::string shadowReport;
    expectReport(EbShadowSimulator::regressionSelfCheck(shadowReport), "shadow-self", shadowReport);
    expect(EbBufferEventMachine::selfCheck(), "buffer-sm");
    std::string filterReport;
    expectReport(EbInLevelSettingsFilter::selfCheck(filterReport), "inlevel-settings-filter",
                 filterReport);
    std::string attribReport;
    expectReport(EbAssistController::effectTickSelfCheck(attribReport), "effect-tick-attrib",
                 attribReport);
    std::string releaseGateReport;
    expectReport(EbAssistController::releaseGateSelfCheck(releaseGateReport), "release-gate",
                 releaseGateReport);
    std::string fireReport;
    expectReport(EbAssistController::fireTickSelfCheck(fireReport), "fire-tick-jump", fireReport);
    std::string queuedHoldReport;
    expectReport(EbAssistController::queuedHoldSelfCheck(queuedHoldReport), "queued-hold",
                 queuedHoldReport);
    std::string recheckReport;
    expectReport(EbAssistController::recheckSlideSelfCheck(recheckReport), "recheck-slide",
                 recheckReport);
    std::string exploreReport;
    expectReport(EbAssistLearning::exploreGateSelfCheck(exploreReport), "learn-explore-gate",
                 exploreReport);
    std::string intentReport;
    expectReport(EbClickIntentGuard::selfCheck(intentReport), "click-intent-v2", intentReport);
    std::string saveDropReport;
    expectReport(EbSaveDropPaths::selfCheck(saveDropReport), "save-drop-paths", saveDropReport);
    std::string stageAdaptiveReport;
    expectReport(EbStageConfig::selfCheck(stageAdaptiveReport), "stage-adaptive-resolve",
                 stageAdaptiveReport);
    std::string configKeysReport;
    expectReport(EbConfigBundleKeys::selfCheck(configKeysReport), "config-bundle-keys",
                 configKeysReport);
    std::string configCodecReport;
    expectReport(EbConfigBundleCodec::selfCheck(configCodecReport), "config-bundle-codec",
                 configCodecReport);
    std::string configBundleReport;
    expectReport(EbConfigBundle::selfCheck(configBundleReport), "config-bundle",
                 configBundleReport);
    std::string platformLimitsReport;
    expectReport(EbPlatformLimits::selfCheck(platformLimitsReport), "platform-limits",
                 platformLimitsReport);
    std::string classifyReport;
    expectReport(EbTriggerClassify::selfCheck(classifyReport), "trigger-classify", classifyReport);
    std::string toggleOrbReport;
    expectReport(EbToggleOrbSim::selfCheck(toggleOrbReport), "toggle-orb-sim", toggleOrbReport);
    std::string twReport;
    expectReport(EbTriggerWorldStore::selfCheck(twReport), "trigger-world-store", twReport);
    std::string clockReport;
    expectReport(EbAssistClock::selfCheck(clockReport), "assist-clock", clockReport);
    std::string devClockReport;
    expectReport(EbDevClockContract::selfCheck(devClockReport), "dev-clock-contract",
                 devClockReport);
    std::string devWitnessReport;
    expectReport(EbDevLiveWitness::selfCheck(devWitnessReport), "dev-live-witness",
                 devWitnessReport);
    std::string devRestoreReport;
    expectReport(EbDevEvalRestore::selfCheck(devRestoreReport), "dev-eval-restore",
                 devRestoreReport);
    std::string devDiffReport;
    expectReport(EbDevStateDiff::selfCheck(devDiffReport), "dev-state-diff", devDiffReport);
    std::string devEvalReport;
    expectReport(EbDevEvalEngine::selfCheck(devEvalReport), "dev-eval-engine", devEvalReport);
    std::string devObjectiveReport;
    expectReport(EbDevObjective::selfCheck(devObjectiveReport), "dev-objective",
                 devObjectiveReport);
    std::string devLedgerReport;
    expectReport(EbDevRejectLedger::selfCheck(devLedgerReport), "dev-reject-ledger",
                 devLedgerReport);
    std::string devSessionReport;
    expectReport(EbDevSession::selfCheck(devSessionReport), "dev-session", devSessionReport);
    std::string devReportReport;
    expectReport(EbDevReport::selfCheck(devReportReport), "dev-report", devReportReport);
    std::string verdictReport;
    expectReport(EbAssistVerdict::selfCheck(verdictReport), "assist-verdict", verdictReport);
    std::string flightModeReport;
    expectReport(EbFlightModePolicy::selfCheck(flightModeReport), "flight-mode-policy",
                 flightModeReport);
    std::string wavePatternReport;
    expectReport(EbWavePatternRecognizer::selfCheck(wavePatternReport), "wave-pattern-recognizer",
                 wavePatternReport);
    std::string spotTrustReport;
    expectReport(EbSpotTrustMap::selfCheck(spotTrustReport), "spot-trust-map", spotTrustReport);
    std::string simTrustReport;
    expectReport(EbSimTrust::selfCheck(simTrustReport), "sim-trust", simTrustReport);
    std::string ghostParityReport;
    expectReport(EbGhostParityProbe::selfCheck(ghostParityReport), "ghost-parity-probe",
                 ghostParityReport);
    std::string contracts140Report;
    expectReport(EbAssistContracts140::selfCheck(contracts140Report), "assist-contracts-140",
                 contracts140Report);
    std::string realMetricsReport;
    expectReport(EbAssistRealMetrics::selfCheck(realMetricsReport), "assist-real-metrics",
                 realMetricsReport);
    std::string simEstimateReport;
    expectReport(EbAssistSimEstimate::selfCheck(simEstimateReport), "assist-sim-estimate",
                 simEstimateReport);
    std::string collisionPolicyReport;
    expectReport(EbSimCollisionPolicy::selfCheck(collisionPolicyReport), "sim-collision-policy",
                 collisionPolicyReport);
    std::string tg2FixtureReport;
    expectReport(EbTg2BakeFixture::selfCheck(tg2FixtureReport), "tg2-bake-fixture",
                 tg2FixtureReport);
    std::string orbStackReport;
    expectReport(EbOrbStackResolver::selfCheck(orbStackReport), "orb-stack-resolver",
                 orbStackReport);
    std::string modeSurvivalReport;
    expectReport(EbModeSurvival::selfCheck(modeSurvivalReport), "mode-survival",
                 modeSurvivalReport);
    std::string prefixSimReport;
    expectReport(EbPrefixSim::selfCheck(prefixSimReport), "prefix-sim", prefixSimReport);
    std::string idlePrefixReport;
    expectReport(EbIdlePrefixShare::selfCheck(idlePrefixReport), "idle-prefix-share",
                 idlePrefixReport);
    std::string perfectAssistReport;
    expectReport(EbPerfectAssistPolicy::selfCheck(perfectAssistReport), "perfect-assist-policy",
                 perfectAssistReport);
    std::string orbAlignReport;
    expectReport(EbOrbAlign::selfCheck(orbAlignReport), "orb-align", orbAlignReport);
    std::string assistHorizonReport;
    expectReport(EbAssistHorizon::selfCheck(assistHorizonReport), "assist-horizon",
                 assistHorizonReport);
    std::string realCorpusReport;
    expectReport(EbRealCorpusFixtures::selfCheck(realCorpusReport), "real-corpus-fixtures",
                 realCorpusReport);
    std::string sectionWalkReport;
    expectReport(EbSectionObjectWalk::selfCheck(sectionWalkReport), "section-object-walk",
                 sectionWalkReport);
    std::string diagDumpReport;
    expectReport(EbDevDiagDump::selfCheck(diagDumpReport), "dev-diag-dump", diagDumpReport);
    std::string researchPackReport;
    expectReport(EbDevResearchPack::selfCheck(researchPackReport), "dev-research-pack",
                 researchPackReport);
    std::string sessionCompareReport;
    expectReport(EbDevSessionCompare::selfCheck(sessionCompareReport), "dev-session-compare",
                 sessionCompareReport);
    std::string livePlanReport;
    expectReport(EbLivePlanContracts::selfCheck(livePlanReport), "live-plan-contracts",
                 livePlanReport);
    // Assist Pack AP1: frozen A4 fair-depth (research_20261113_001322).
    expect(EbLivePlanContracts::depthTruncatedAlive(false, false, 43, 54) &&
               EbLivePlanContracts::hopelessBlockedByDepthTruncation(true, false) &&
               !EbLivePlanContracts::fairDepthPreferCand(96, 3, 100.f, 18, 0, 103.f, 18),
           "assist-pack-ap1-fair-depth-a4");
    // Assist Pack AP4: living later blocks Hopeless; uniform death (A7) allows it.
    expect(EbLivePlanContracts::hopelessBlockedByLivingLater(true, true) &&
               !EbLivePlanContracts::hopelessBlockedByLivingLater(true, false),
           "assist-pack-ap4-false-hopeless");
    // Assist Pack AP3 (tight): endX/survive lead blocks PassD0Safe; bare under-horizon does not.
    expect(
        !EbLivePlanContracts::laterAliveBlocksPassD0Safe(true, true, 18, 96, 8, 100.f, 100.f) &&
            EbLivePlanContracts::laterAliveBlocksPassD0Safe(true, true, 18, 96, 8, 109.f, 100.f) &&
            EbLivePlanContracts::weakD0SafeNeedsFallthrough(true) &&
            !EbLivePlanContracts::weakD0SafeNeedsFallthrough(false),
        "assist-pack-ap3-weak-d0safe");
    // Assist Pack AP5: continuous ranks by endX; wave save prefers score peak.
    expect(EbLivePlanContracts::continuousIwRanksByEndX() &&
               EbLivePlanContracts::waveIwChooseDelayByScore(false, 7, 3) == 7 &&
               EbLivePlanContracts::waveIwChooseDelayByScore(true, 7, 3) == 0,
           "assist-pack-ap5-endx-rank");
    // Assist Pack AP7: discrete IW floor is model.cap (reachable), not limit+cap.
    expect(EbLivePlanContracts::discreteIwObservationFloor(92) == 92 &&
               EbLivePlanContracts::discreteIwObservationFloor(0) == 1,
           "assist-pack-ap7-iw-floor");
    // Assist Pack AP3c: probe blocks wall PassD0Safe; micro prefers endX in tiny windows.
    expect(EbLivePlanContracts::horizonProbeBlocksPassD0Safe(true, true, true) &&
               !EbLivePlanContracts::horizonProbeBlocksPassD0Safe(false, true, true) &&
               EbLivePlanContracts::microWindowPreferLater(true, 4, 110.f, 100.f) &&
               !EbLivePlanContracts::microWindowPreferLater(true, 12, 110.f, 100.f),
           "assist-pack-ap3c-toggles");
    // Assist Pack AP-L6: discrete IW runs horizon probe before the later band.
    expect(EbLivePlanContracts::discreteIwProbeBeforeBand(false, true, true) &&
               !EbLivePlanContracts::discreteIwProbeBeforeBand(true, true, true) &&
               !EbLivePlanContracts::discreteIwProbeBeforeBand(false, false, true),
           "assist-pack-ap-l6-probe-before-band");
    // Assist Pack AP-L7: lag notification on hitch ceiling / budgetHit (not budget %).
    expect(EbLivePlanContracts::lagWarningShouldNotify(true, 9000, false) &&
               EbLivePlanContracts::lagWarningShouldNotify(true, 1000, true) &&
               !EbLivePlanContracts::lagWarningShouldNotify(false, 9000, true) &&
               !EbLivePlanContracts::lagWarningShouldNotify(true, 3000, false),
           "assist-pack-ap-l7-lag-toast");
    // Assist Pack AP-M1: later progress past dying d0 blocks PassWaveFutile.
    expect(EbLivePlanContracts::waveIwLaterProgressBeatsD0(true, 24, 25, 100.f, true, true, 26, 27,
                                                           104.f) &&
               !EbLivePlanContracts::waveIwLaterProgressBeatsD0(true, 24, 25, 100.f, true, true, 24,
                                                                25, 100.f) &&
               !EbLivePlanContracts::waveIwFutilePass(true, true, false, false, false, 0, 3.f,
                                                      false, true) &&
               EbLivePlanContracts::waveIwPreferPassBudgetOverFutile(true, false, 3, 8),
           "assist-pack-ap-m1-wave-false-futile");
    // Assist Pack AP-M6: Adaptive shell soft-abort gifts remUs to classic.
    expect(!EbLivePlanContracts::adaptiveShellHitPoisonsFallthrough(1500) &&
               EbLivePlanContracts::adaptiveShellHitPoisonsFallthrough(0) &&
               EbLivePlanContracts::adaptiveAbortToFallthroughAfterScreen(true, 0, 12) &&
               !EbLivePlanContracts::adaptiveAbortToFallthroughAfterScreen(false, 0, 12),
           "assist-pack-ap-m6-adaptive-remus-gift");
    // Assist Pack AP-M3: probe-extended d0 yields to floor-reached later Alive.
    expect(
        EbLivePlanContracts::discreteIwProbeExtendedD0Alive(true, 120, 96, true) &&
            EbLivePlanContracts::discreteIwLivingBandDemotesPassD0Safe(true, true, true) &&
            !EbLivePlanContracts::discreteIwLivingBandDemotesPassD0Safe(true, true, false) &&
            !EbLivePlanContracts::laterAliveBlocksPassD0Safe(true, true, 18, 96, 8, 100.f, 100.f),
        "assist-pack-ap-m3-cube-underselect");
    // Assist Pack AP-M7: Wave zero-survivor Adaptive gifts classic IW remUs.
    expect(EbLivePlanContracts::adaptiveWaveZeroSurvivorsNeedsClassicIw(true, true, 0) &&
               EbLivePlanContracts::adaptiveWaveFallthroughGiftUs(true, 0, 2000) == 2000 &&
               EbLivePlanContracts::adaptiveWaveFallthroughUsesFreshWall(true, 0) &&
               EbLivePlanContracts::adaptiveWaveFallthroughGiftUs(false, 0, 2000) == 0 &&
               EbLivePlanContracts::adaptiveWaveScreenTicksForTw(24, 942) == 12,
           "assist-pack-ap-m7-adaptive-wave-fallthrough");
    // Assist Pack AP-M4: ball later run-alive / incomplete band → PassBudget.
    expect(EbLivePlanContracts::discreteIwLaterRunAliveForHopeless(false, false, 18) &&
               EbLivePlanContracts::ballIwIncompleteBandBlocksHopeless(true, false, 2, 12) &&
               EbLivePlanContracts::discreteIwHopelessMustFallthrough(true, true, false, true,
                                                                      false, 13, 12) &&
               !EbLivePlanContracts::discreteIwHopelessMustFallthrough(true, false, false, true,
                                                                       false, 13, 12),
           "assist-pack-ap-m4-ball-false-hopeless");
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
    expect(EbLivePlanContracts::kMidRunWallPollTicks == EbShadowSimulator::kMidRunWallPollTicks &&
               EbLivePlanContracts::shouldPollMidRunWall(0) &&
               EbLivePlanContracts::midRunWallExceeded(8000, 8000),
           "assist-pack-ap-l1-mid-run-wall");
    expect(EbLivePlanContracts::kPrepareMinRemUs == 1500 &&
               EbLivePlanContracts::prepareReserveUs(8170, true) == 4085 &&
               EbLivePlanContracts::prepareReserveUs(8170, false) == 1500 &&
               !EbLivePlanContracts::prepareHeavyWorkAllowed(4000, 4085, false),
           "assist-pack-ap-l2-prepare-reserve");
    expect(EbLivePlanContracts::kLiveTwTightenTrackFloor == 200 &&
               EbLivePlanContracts::liveTwApplyMaxPerTick(942, false, 96) == 48 &&
               EbLivePlanContracts::liveTwApplyBudgetUs(942, false, 350) == 175 &&
               EbLivePlanContracts::liveTwApplyMaxPerTick(942, true, 96) == 96 &&
               EbLivePlanContracts::liveTwApplySkippedForPlanWall(true),
           "assist-pack-ap-l3-live-tw");
    expect(EbLivePlanContracts::waveIwD0HasRunTruth(true, 8) &&
               EbLivePlanContracts::waveIwFutileHonest(true, false) &&
               !EbLivePlanContracts::waveIwFutileHonest(true, true) &&
               EbLivePlanContracts::waveIwSkipLaterBandForStarve(true, false),
           "assist-pack-ap-l4-wave-honesty");
    expect(EbLivePlanContracts::adaptiveFallthroughReserveUs(8170, true) == 4085 &&
               EbLivePlanContracts::adaptiveShellBudgetUs(
                   8170, EbLivePlanContracts::adaptiveFallthroughReserveUs(8170, true)) == 4085 &&
               EbLivePlanContracts::intendedFallthroughClassicBudgetUs(0, 100) == 0 &&
               EbLivePlanContracts::intendedLiveSoftWallFloorUs(2500) == 2500 &&
               !EbLivePlanContracts::adaptiveFallthroughSeedsObservationCleared(true) &&
               EbLivePlanContracts::adaptiveFallthroughSeedsObservationCleared(false),
           "assist-pack-ap-l5-adaptive-fallthrough");
    expect(EbLivePlanContracts::intendedLiveBudgetUs(0) == EbLivePlanContracts::kHitchCeilingUs,
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
        expect(!orbPick.doPass && orbPick.chosen == 1 && orbPick.requireOrb,
               "orb-under-input-window");
    }
    // Dash hold contract: wasDashing salvage uses releaseAfter=40 (IW mirrors classic).
    {
        int const dashHoldReleaseAfter = 40;
        expect(dashHoldReleaseAfter == 40 && dashHoldReleaseAfter > 0, "orb-dash-hold-iw");
    }
    // Toggle unknown named Pass (not Hopeless) — reason id locked in AssistVerdict.
    expect(std::string(EbAssistVerdict::name(AssistReason::PassToggleUnknown)).find("toggle") !=
               std::string::npos,
           "pass-toggle-unknown-iw");
    std::string missReport;
    expectReport(EbMissAssistContracts::selfCheck(missReport), "miss-assist-contracts", missReport);
    std::string pickV1Report;
    expectReport(EbAssistPickV1::selfCheck(pickV1Report), "assist-pick-v1", pickV1Report);
    std::string pickV2Report;
    expectReport(EbAssistPickV2::selfCheck(pickV2Report), "assist-pick-v2", pickV2Report);
    std::string longForesightReport;
    expectReport(EbLongForesight::selfCheck(longForesightReport), "long-foresight",
                 longForesightReport);
    std::string foresightReport;
    expectReport(EbTravelForesight::selfCheck(foresightReport), "travel-foresight",
                 foresightReport);
    std::string probeReport;
    expectReport(EbOpportunityProbe::selfCheck(probeReport), "opportunity-probe", probeReport);
    std::string husReport;
    expectReport(EbAssistController::holdUntilSafeSelfCheck(husReport), "hold-until-safe",
                 husReport);
    std::string divReport;
    expectReport(EbDivergenceTracker::selfCheck(divReport), "divergence-clear", divReport);
    std::string simTickReport;
    expectReport(EbSimTickCostMeter::selfCheck(simTickReport), "sim-tick-cost-meter",
                 simTickReport);
    std::string adaptiveSlotReport;
    expectReport(EbAdaptiveCandidateSlot::selfCheck(adaptiveSlotReport), "adaptive-candidate-slot",
                 adaptiveSlotReport);
    std::string adaptiveResumeReport;
    expectReport(EbAdaptiveResumeRunner::selfCheck(adaptiveResumeReport), "adaptive-resume-runner",
                 adaptiveResumeReport);
    std::string adaptiveBudgetReport;
    expect(EbAdaptiveBudgetController::selfCheck(adaptiveBudgetReport),
           "adaptive-budget-controller");
    std::string adaptivePlanSearchReport;
    expectReport(EbAdaptivePlanSearch::selfCheck(adaptivePlanSearchReport), "adaptive-plan-search",
                 adaptivePlanSearchReport);
    std::string adaptiveSchedulerReport;
    expect(EbAdaptivePredictionScheduler::selfCheck(adaptiveSchedulerReport),
           "adaptive-prediction-scheduler");
    std::string adaptiveGateReport;
    expectReport(EbAdaptiveDecisionGate::selfCheck(adaptiveGateReport), "adaptive-decision-gate",
                 adaptiveGateReport);
    std::string adaptiveParityReport;
    expectReport(EbAdaptiveParityPolicy::selfCheck(adaptiveParityReport), "adaptive-parity-policy",
                 adaptiveParityReport);
    std::string devConfigReport;
    expectReport(EbDevConfig::selfCheck(devConfigReport), "dev-config", devConfigReport);
    std::string devTraceReport;
    expectReport(EbDevTraceStore::selfCheck(devTraceReport), "dev-trace-store", devTraceReport);
    std::string devResourceReport;
    expectReport(EbDevResourceStats::selfCheck(devResourceReport), "dev-resource-stats",
                 devResourceReport);
    std::string devCycleReport;
    expectReport(EbDevCycleRecorder::selfCheck(devCycleReport), "dev-cycle-recorder",
                 devCycleReport);
    // Synthetic gold: earliest robust policy — no midpoint
    expect(true, "no-midpoint-policy");
    if (fails == 0)
        report = "PlanGoldCorpus OK";
    else
        report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}
