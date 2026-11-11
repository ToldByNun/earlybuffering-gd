#include "live_plan_contracts.hpp"
#include <cmath>
#include <fmt/format.h>

int EbLivePlanContracts::currentClassicBudgetUs(int configuredBudgetUs) {
    if (configuredBudgetUs > 0)
        return configuredBudgetUs;
    return kHitchCeilingUs;
}

int EbLivePlanContracts::currentAdaptiveBudgetUs(int configuredBudgetUs) {
    return configuredBudgetUs;
}

int EbLivePlanContracts::intendedLiveBudgetUs(int configuredBudgetUs) {
    if (configuredBudgetUs > 0)
        return configuredBudgetUs;
    return kHitchCeilingUs;
}

int EbLivePlanContracts::currentFallthroughClassicBudgetUs(int remUs, int ticksLeft) {
    if (remUs > 0)
        return remUs;
    if (ticksLeft > 0)
        return 1;
    return 0;
}

int EbLivePlanContracts::intendedFallthroughClassicBudgetUs(int remUs, int ticksLeft) {
    if (remUs > 0)
        return remUs;
    if (ticksLeft > 0)
        return kFallthroughRemUsFloor;
    return 0;
}

bool EbLivePlanContracts::currentLandedPreferLater(float bestScore, float candScore, int bestDelay,
                                                   int candDelay) {
    float const gain = candScore - bestScore;
    if (gain > kLandedScoreEps)
        return true;
    if (std::fabs(gain) <= kLandedScoreEps && candDelay < bestDelay)
        return true;
    return false;
}

bool EbLivePlanContracts::intendedLandedPreferLater(float bestScore, float candScore, int bestDelay,
                                                    int candDelay) {
    // Eval-aligned: any score improve wins (no 8px barrier). Exact tie → earlier.
    if (candScore > bestScore + 1e-4f)
        return true;
    if (std::fabs(candScore - bestScore) <= 1e-4f && candDelay < bestDelay)
        return true;
    return false;
}

bool EbLivePlanContracts::peakDeathShouldStop(bool inputWindow, bool continuousLike, bool hadSurvivor,
                                              bool diedAfterAction) {
    return inputWindow && !continuousLike && hadSurvivor && diedAfterAction;
}

bool EbLivePlanContracts::peakDeathMarksSkippedNotDead() {
    return true;
}

bool EbLivePlanContracts::currentOrbWindowScanRuns(bool assistOrbs, bool inputWindow) {
    return assistOrbs && !inputWindow;
}

bool EbLivePlanContracts::intendedSpatialOrbHintAllowed(bool assistOrbs) {
    return assistOrbs;
}

bool EbLivePlanContracts::intendedOrbWindowScanAllowed(bool assistOrbs, bool inputWindow,
                                                       bool budgetOk) {
    (void)inputWindow;
    return assistOrbs && budgetOk;
}

bool EbLivePlanContracts::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    expect(kHitchCeilingUs == 8000, "hitch-ceiling");
    expect(kFallthroughRemUsFloor == 2000, "remus-floor");
    expect(std::fabs(kLandedScoreEps - 8.f) < 1e-6f, "landed-eps");

    expect(currentClassicBudgetUs(4000) == 4000, "classic-keep");
    expect(currentClassicBudgetUs(0) == kHitchCeilingUs, "classic-zero-clamp");
    expect(currentAdaptiveBudgetUs(0) == 0, "adaptive-zero-unlimited-today");
    expect(currentAdaptiveBudgetUs(4000) == 4000, "adaptive-keep");
    expect(intendedLiveBudgetUs(0) == kHitchCeilingUs, "intended-clamp-both");
    expect(intendedLiveBudgetUs(5000) == 5000, "intended-keep");

    expect(currentFallthroughClassicBudgetUs(1500, 100) == 1500, "ft-rem-keep");
    expect(currentFallthroughClassicBudgetUs(0, 100) == 1, "ft-rem-one-today");
    expect(currentFallthroughClassicBudgetUs(0, 0) == 0, "ft-rem-done");
    expect(intendedFallthroughClassicBudgetUs(0, 100) == kFallthroughRemUsFloor, "ft-rem-floor");
    expect(intendedFallthroughClassicBudgetUs(900, 50) == 900, "ft-rem-intended-keep");

    // Current overshoot: +3px does not beat d0; intended Eval-aligned does.
    expect(!currentLandedPreferLater(100.f, 103.f, 0, 5), "cur-landed-gate");
    expect(intendedLandedPreferLater(100.f, 103.f, 0, 5), "int-landed-peak");
    // Current: +9px prefers later; intended also prefers later on clear gain.
    expect(currentLandedPreferLater(100.f, 109.f, 0, 5), "cur-landed-clear");
    expect(intendedLandedPreferLater(100.f, 109.f, 0, 5), "int-landed-clear");
    // Near-tie: both prefer earlier delay.
    expect(currentLandedPreferLater(100.f, 100.f, 5, 2), "cur-landed-tie-earlier");
    expect(intendedLandedPreferLater(100.f, 100.f, 5, 2), "int-landed-tie-earlier");
    expect(!intendedLandedPreferLater(100.f, 100.f, 2, 5), "int-landed-tie-not-later");

    expect(peakDeathShouldStop(true, false, true, true), "peak-stop");
    expect(!peakDeathShouldStop(true, true, true, true), "peak-no-continuous");
    expect(!peakDeathShouldStop(true, false, false, true), "peak-need-survivor");
    expect(!peakDeathShouldStop(false, false, true, true), "peak-need-iw");
    expect(peakDeathMarksSkippedNotDead(), "peak-skip-not-dead");

    expect(currentOrbWindowScanRuns(true, false), "orb-scan-classic");
    expect(!currentOrbWindowScanRuns(true, true), "orb-scan-iw-skipped-today");
    expect(intendedSpatialOrbHintAllowed(true), "orb-hint-iw-intended");
    expect(!intendedSpatialOrbHintAllowed(false), "orb-hint-off");
    expect(intendedOrbWindowScanAllowed(true, true, true), "orb-scan-iw-intended");
    expect(!intendedOrbWindowScanAllowed(true, true, false), "orb-scan-budget");

    if (fails == 0) {
        report = "LivePlanContracts selfCheck OK";
        return true;
    }
    report = fmt::format("LivePlanContracts selfCheck {} fails: {}", fails, report);
    return false;
}
