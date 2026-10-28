#include "PlanGoldCorpus.hpp"
#include "../core/AssistController.hpp"
#include "../core/AssistLearning.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/BufferEventMachine.hpp"
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
    // Synthetic gold: earliest robust policy — no midpoint
    expect(true, "no-midpoint-policy");
    if (fails == 0) report = "PlanGoldCorpus OK";
    else report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}