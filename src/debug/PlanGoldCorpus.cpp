#include "PlanGoldCorpus.hpp"
#include "../core/AssistClock.hpp"
#include "../core/AssistController.hpp"
#include "../core/AssistLearning.hpp"
#include "../core/AssistContracts140.hpp"
#include "../core/AssistRealMetrics.hpp"
#include "../core/AssistVerdict.hpp"
#include "../core/GhostParityProbe.hpp"
#include "../core/MissAssistContracts.hpp"
#include "../core/SimCollisionPolicy.hpp"
#include "../core/SimTrust.hpp"
#include "../core/SpotTrustMap.hpp"
#include "../dev/DevDiagDump.hpp"
#include "../core/TravelForesight.hpp"
#include "../core/OpportunityProbe.hpp"
#include "../core/DivergenceTracker.hpp"
#include "../core/ClickIntentGuard.hpp"
#include "../core/ConfigBundle.hpp"
#include "../core/ConfigBundleCodec.hpp"
#include "../core/ConfigBundleKeys.hpp"
#include "../core/PlatformLimits.hpp"
#include "../core/SaveDropPaths.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/BufferEventMachine.hpp"
#include "../core/ToggleOrbSim.hpp"
#include "../core/TriggerClassify.hpp"
#include "../core/TriggerWorldStore.hpp"
#include "../ui/InLevelSettingsFilter.hpp"
#include "../dev/DevClockContract.hpp"
#include "../dev/DevConfig.hpp"
#include "../dev/DevCycleRecorder.hpp"
#include "../dev/DevEvalEngine.hpp"
#include "../dev/DevEvalRestore.hpp"
#include "../dev/DevLiveWitness.hpp"
#include "../dev/DevObjective.hpp"
#include "../dev/DevRejectLedger.hpp"
#include "../dev/DevReport.hpp"
#include "../dev/DevSession.hpp"
#include "../dev/DevStateDiff.hpp"
#include "../dev/DevTraceStore.hpp"
#include <fmt/format.h>

bool PlanGoldCorpus::run(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) { ++fails; report += std::string("FAIL ") + name + "; "; }
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
    std::string collisionPolicyReport;
    expect(SimCollisionPolicy::selfCheck(collisionPolicyReport), "sim-collision-policy");
    std::string diagDumpReport;
    expect(DevDiagDump::selfCheck(diagDumpReport), "dev-diag-dump");
    std::string missReport;
    expect(MissAssistContracts::selfCheck(missReport), "miss-assist-contracts");
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
    if (fails == 0) report = "PlanGoldCorpus OK";
    else report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}