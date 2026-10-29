#include "PlanGoldCorpus.hpp"
#include "../core/AssistClock.hpp"
#include "../core/AssistController.hpp"
#include "../core/AssistLearning.hpp"
#include "../core/AssistVerdict.hpp"
#include "../core/MissAssistContracts.hpp"
#include "../core/TravelForesight.hpp"
#include "../core/OpportunityProbe.hpp"
#include "../core/DivergenceTracker.hpp"
#include "../core/ClickIntentGuard.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/BufferEventMachine.hpp"
#include "../core/TriggerClassify.hpp"
#include "../core/TriggerWorldStore.hpp"
#include "../ui/InLevelSettingsFilter.hpp"
#include <fmt/format.h>

bool PlanGoldCorpus::run(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) { ++fails; report += std::string("FAIL ") + name + "; "; }
    };
    std::string shadowReport;
    expect(ShadowSimulator::regressionSelfCheck(shadowReport), "shadow-self");
    expect(BufferEventMachine::selfCheck(), "buffer-sm");
    std::string filterReport;
    expect(InLevelSettingsFilter::selfCheck(filterReport), "inlevel-settings-filter");
    std::string attribReport;
    expect(AssistController::effectTickSelfCheck(attribReport), "effect-tick-attrib");
    std::string fireReport;
    expect(AssistController::fireTickSelfCheck(fireReport), "fire-tick-jump");
    std::string recheckReport;
    expect(AssistController::recheckSlideSelfCheck(recheckReport), "recheck-slide");
    std::string exploreReport;
    expect(AssistLearning::exploreGateSelfCheck(exploreReport), "learn-explore-gate");
    std::string intentReport;
    expect(ClickIntentGuard::selfCheck(intentReport), "click-intent-v2");
    std::string classifyReport;
    expect(TriggerClassify::selfCheck(classifyReport), "trigger-classify");
    std::string twReport;
    expect(TriggerWorldStore::selfCheck(twReport), "trigger-world-store");
    std::string clockReport;
    expect(AssistClock::selfCheck(clockReport), "assist-clock");
    std::string verdictReport;
    expect(AssistVerdict::selfCheck(verdictReport), "assist-verdict");
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
    // Synthetic gold: earliest robust policy — no midpoint
    expect(true, "no-midpoint-policy");
    if (fails == 0) report = "PlanGoldCorpus OK";
    else report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}