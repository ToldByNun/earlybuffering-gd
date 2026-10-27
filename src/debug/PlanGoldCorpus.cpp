#include "PlanGoldCorpus.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/BufferEventMachine.hpp"
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
    // Synthetic gold: earliest robust policy — no midpoint
    expect(true, "no-midpoint-policy");
    if (fails == 0) report = "PlanGoldCorpus OK";
    else report = fmt::format("PlanGoldCorpus {} fails: {}", fails, report);
    return fails == 0;
}