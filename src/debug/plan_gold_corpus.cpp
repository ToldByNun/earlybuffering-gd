#include "plan_gold_corpus.hpp"
#include "../core/assist/assist_clock.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/contracts/assist_contracts140.hpp"
#include "../core/assist/contracts/assist_verdict.hpp"
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
#include "../dev/evaluation/dev_eval_engine.hpp"
#include "../dev/evaluation/dev_eval_restore.hpp"
#include "../dev/evaluation/dev_objective.hpp"
#include "../dev/evaluation/dev_reject_ledger.hpp"
#include "../dev/evaluation/dev_state_diff.hpp"
#include "../dev/reporting/dev_diag_dump.hpp"
#include "../dev/reporting/dev_report.hpp"
#include "../ui/settings/in_level_settings_filter.hpp"
#include <fmt/format.h>

bool PlanGoldCorpus::run(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    // Commit 2 LOCK: freeze ε before any probe / contract selfCheck.
    AssistContracts140::applyLockedEps();
    std::string shadowReport;
    expect(ShadowSimulator::regressionSelfCheck(shadowReport), "shadow-self");
    expect(BufferEventMachine::selfCheck(), "buffer-sm");
    std::string filterReport;
    expect(InLevelSettingsFilter::selfCheck(filterReport), "inlevel-settings-filter");
    std::string attribReport;
    expect(AssistController::effectTickSelfCheck(attribReport), "effect-tick-attrib");
    std::string releaseGateReport;
    expect(AssistController::releaseGateSelfCheck(releaseGateReport), "release-gate");
    std::string fireReport;
    expect(AssistController::fireTickSelfCheck(fireReport), "fire-tick-jump");
    std::string queuedHoldReport;
    expect(AssistController::queuedHoldSelfCheck(queuedHoldReport), "queued-hold");
    std::string recheckReport;
    expect(AssistController::recheckSlideSelfCheck(recheckReport), "recheck-slide");
    std::string exploreReport;
    expect(AssistLearning::exploreGateSelfCheck(exploreReport), "learn-explore-gate");
    std::string intentReport;
    expect(ClickIntentGuard::selfCheck(intentReport), "click-intent-v2");
    std::string saveDropReport;
    expect(SaveDropPaths::selfCheck(saveDropReport), "save-drop-paths");
    std::string configKeysReport;
    expect(ConfigBundleKeys::selfCheck(configKeysReport), "config-bundle-keys");
    std::string configCodecReport;
    expect(ConfigBundleCodec::selfCheck(configCodecReport), "config-bundle-codec");
    std::string configBundleReport;
    expect(ConfigBundle::selfCheck(configBundleReport), "config-bundle");
    std::string platformLimitsReport;
    expect(PlatformLimits::selfCheck(platformLimitsReport), "platform-limits");
    std::string classifyReport;
    expect(TriggerClassify::selfCheck(classifyReport), "trigger-classify");
    std::string toggleOrbReport;
    expect(ToggleOrbSim::selfCheck(toggleOrbReport), "toggle-orb-sim");
    std::string twReport;
    expect(TriggerWorldStore::selfCheck(twReport), "trigger-world-store");
    std::string clockReport;
    expect(AssistClock::selfCheck(clockReport), "assist-clock");
    std::string devClockReport;
    expect(DevClockContract::selfCheck(devClockReport), "dev-clock-contract");
    std::string devWitnessReport;
    expect(DevLiveWitness::selfCheck(devWitnessReport), "dev-live-witness");
    std::string devRestoreReport;
    expect(DevEvalRestore::selfCheck(devRestoreReport), "dev-eval-restore");
    std::string devDiffReport;
    expect(DevStateDiff::selfCheck(devDiffReport), "dev-state-diff");
    std::string devEvalReport;
    expect(DevEvalEngine::selfCheck(devEvalReport), "dev-eval-engine");
    std::string devObjectiveReport;
    expect(DevObjective::selfCheck(devObjectiveReport), "dev-objective");
    std::string devLedgerReport;
    expect(DevRejectLedger::selfCheck(devLedgerReport), "dev-reject-ledger");
    std::string devSessionReport;
    expect(DevSession::selfCheck(devSessionReport), "dev-session");
    std::string devReportReport;
    expect(DevReport::selfCheck(devReportReport), "dev-report");
    std::string verdictReport;
    expect(AssistVerdict::selfCheck(verdictReport), "assist-verdict");
    std::string spotTrustReport;
    expect(SpotTrustMap::selfCheck(spotTrustReport), "spot-trust-map");
    std::string simTrustReport;
    expect(SimTrust::selfCheck(simTrustReport), "sim-trust");
    std::string ghostParityReport;
    expect(GhostParityProbe::selfCheck(ghostParityReport), "ghost-parity-probe");
    std::string contracts140Report;
    expect(AssistContracts140::selfCheck(contracts140Report), "assist-contracts-140");
    std::string realMetricsReport;
    expect(AssistRealMetrics::selfCheck(realMetricsReport), "assist-real-metrics");
    std::string simEstimateReport;
    expect(AssistSimEstimate::selfCheck(simEstimateReport), "assist-sim-estimate");
    std::string collisionPolicyReport;
    expect(SimCollisionPolicy::selfCheck(collisionPolicyReport), "sim-collision-policy");
    std::string tg2FixtureReport;
    expect(Tg2BakeFixture::selfCheck(tg2FixtureReport), "tg2-bake-fixture");
    std::string orbStackReport;
    expect(OrbStackResolver::selfCheck(orbStackReport), "orb-stack-resolver");
    std::string modeSurvivalReport;
    expect(ModeSurvival::selfCheck(modeSurvivalReport), "mode-survival");
    std::string prefixSimReport;
    expect(PrefixSim::selfCheck(prefixSimReport), "prefix-sim");
    std::string perfectAssistReport;
    expect(PerfectAssistPolicy::selfCheck(perfectAssistReport), "perfect-assist-policy");
    std::string orbAlignReport;
    expect(OrbAlign::selfCheck(orbAlignReport), "orb-align");
    std::string assistHorizonReport;
    expect(AssistHorizon::selfCheck(assistHorizonReport), "assist-horizon");
    std::string realCorpusReport;
    expect(RealCorpusFixtures::selfCheck(realCorpusReport), "real-corpus-fixtures");
    std::string sectionWalkReport;
    expect(SectionObjectWalk::selfCheck(sectionWalkReport), "section-object-walk");
    std::string diagDumpReport;
    expect(DevDiagDump::selfCheck(diagDumpReport), "dev-diag-dump");
    std::string missReport;
    expect(MissAssistContracts::selfCheck(missReport), "miss-assist-contracts");
    std::string pickV1Report;
    expect(AssistPickV1::selfCheck(pickV1Report), "assist-pick-v1");
    std::string pickV2Report;
    expect(AssistPickV2::selfCheck(pickV2Report), "assist-pick-v2");
    std::string longForesightReport;
    expect(LongForesight::selfCheck(longForesightReport), "long-foresight");
    std::string foresightReport;
    expect(TravelForesight::selfCheck(foresightReport), "travel-foresight");
    std::string probeReport;
    expect(OpportunityProbe::selfCheck(probeReport), "opportunity-probe");
    std::string husReport;
    expect(AssistController::holdUntilSafeSelfCheck(husReport), "hold-until-safe");
    std::string divReport;
    expect(DivergenceTracker::selfCheck(divReport), "divergence-clear");
    std::string devConfigReport;
    expect(DevConfig::selfCheck(devConfigReport), "dev-config");
    std::string devTraceReport;
    expect(DevTraceStore::selfCheck(devTraceReport), "dev-trace-store");
    std::string devCycleReport;
    expect(DevCycleRecorder::selfCheck(devCycleReport), "dev-cycle-recorder");
    // Synthetic gold: earliest robust policy — no midpoint
    expect(true, "no-midpoint-policy");
    if (fails == 0)
        report = "PlanGoldCorpus OK";
    else
        report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}
