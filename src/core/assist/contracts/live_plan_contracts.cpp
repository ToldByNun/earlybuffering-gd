#include "live_plan_contracts.hpp"
#include <algorithm>
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

bool EbLivePlanContracts::shouldPollMidRunWall(int tickIndex) {
    if (tickIndex < 0)
        return false;
    return (tickIndex % kMidRunWallPollTicks) == 0;
}

bool EbLivePlanContracts::midRunWallExceeded(int budgetUs, long long elapsedUs) {
    if (budgetUs <= 0)
        return false;
    return elapsedUs >= budgetUs;
}

int EbLivePlanContracts::prepareReserveUs(int budgetUs, bool continuousInputWindow) {
    if (budgetUs <= 0)
        return 0;
    if (continuousInputWindow)
        return std::max(kPrepareMinRemUs, budgetUs / 2);
    return kPrepareMinRemUs;
}

bool EbLivePlanContracts::prepareHeavyWorkAllowed(int remUs, int reserveUs, bool wallUnlimited) {
    if (wallUnlimited)
        return true;
    if (remUs <= 0)
        return false;
    if (reserveUs <= 0)
        return true;
    return remUs >= reserveUs;
}

bool EbLivePlanContracts::liveTwApplySkippedForPlanWall(bool planWallExceeded) {
    return planWallExceeded;
}

size_t EbLivePlanContracts::liveTwApplyMaxPerTick(size_t trackCount, bool bakeOffline,
                                                   size_t fullCap) {
    size_t const cap = fullCap > 0 ? fullCap : 1;
    if (bakeOffline || trackCount < static_cast<size_t>(kLiveTwTightenTrackFloor))
        return cap;
    return std::max<size_t>(1, cap / 2);
}

int EbLivePlanContracts::liveTwApplyBudgetUs(size_t trackCount, bool bakeOffline, int fullBudgetUs) {
    int const budget = fullBudgetUs > 0 ? fullBudgetUs : 1;
    if (bakeOffline || trackCount < static_cast<size_t>(kLiveTwTightenTrackFloor))
        return budget;
    return std::max(1, budget / 2);
}

int EbLivePlanContracts::currentFallthroughClassicBudgetUs(int remUs, int ticksLeft) {
    if (remUs > 0)
        return remUs;
    if (ticksLeft > 0)
        return 1;
    return 0;
}

int EbLivePlanContracts::intendedFallthroughClassicBudgetUs(int remUs, int ticksLeft) {
    (void)ticksLeft;
    // AP-L5: shared press wall — never invent µs past sim-budget-us.
    return remUs > 0 ? remUs : 0;
}

int EbLivePlanContracts::intendedLiveSoftWallFloorUs(int budgetUs) {
    if (budgetUs <= 0)
        return budgetUs;
    if (budgetUs < kLiveSoftWallFloorUs)
        return kLiveSoftWallFloorUs;
    return budgetUs;
}

int EbLivePlanContracts::adaptiveShellBudgetUs(int fullBudgetUs, int fallthroughReserveUs) {
    if (fullBudgetUs <= 0)
        return 0;
    int const reserve = std::max(0, fallthroughReserveUs);
    if (reserve >= fullBudgetUs)
        return 1;
    return fullBudgetUs - reserve;
}

int EbLivePlanContracts::adaptiveFallthroughReserveUs(int fullBudgetUs,
                                                      bool discreteInputWindow) {
    if (fullBudgetUs <= 0)
        return 0;
    if (!discreteInputWindow)
        return kFallthroughRemUsFloor;
    int const half = fullBudgetUs / 2;
    int const prepareFloor = prepareReserveUs(fullBudgetUs, false);
    return std::max(kFallthroughRemUsFloor, std::max(prepareFloor * 2, half));
}

bool EbLivePlanContracts::adaptiveFallthroughSeedsObservationCleared(bool isBall) {
    return !isBall;
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

int EbLivePlanContracts::currentContinuousIwCap(int continuousLookAhead) {
    return continuousLookAhead > 0 ? continuousLookAhead : 1;
}

int EbLivePlanContracts::waveShortPlanCap(int limit) {
    int const x = limit > 0 ? limit : 1;
    return std::max(kWaveHorizonFloor, x + 16);
}

int EbLivePlanContracts::intendedContinuousIwCap(int continuousLookAhead,
                                                 int shortAssistHorizonPlanCap) {
    int const look = continuousLookAhead > 0 ? continuousLookAhead : 1;
    int const plan = shortAssistHorizonPlanCap > 0 ? shortAssistHorizonPlanCap : 1;
    return std::max(look, plan);
}

bool EbLivePlanContracts::waveIwHelpOnlyPass(bool isWave, bool pressDown, bool d0ObservationSafe) {
    return isWave && pressDown && d0ObservationSafe;
}

bool EbLivePlanContracts::waveIwProximityPass(bool isWave, bool pressDown, bool d0Died,
                                              bool nearHazard, bool noOrbTarget,
                                              bool foresightOrbPad) {
    if (!pressDown || !isWave || d0Died || !nearHazard || !noOrbTarget)
        return false;
    return !foresightOrbPad;
}

bool EbLivePlanContracts::waveIwFutilePass(bool isWave, bool pressDown, bool anyAliveSaveFromD0Death,
                                           bool idleHoldsEscape, bool orbIntent, int bestSurviveBin,
                                           float bestEndXGain, bool foresightOrbPad) {
    if (!pressDown || !isWave || anyAliveSaveFromD0Death || idleHoldsEscape || orbIntent ||
        foresightOrbPad)
        return false;
    if (bestSurviveBin > 0)
        return false;
    return !(bestEndXGain >= kLandedScoreEps);
}

bool EbLivePlanContracts::waveIwD0HasRunTruth(bool candReady0, int d0TicksRan) {
    return candReady0 && d0TicksRan > 0;
}

bool EbLivePlanContracts::waveIwFutileHonest(bool d0HasRunTruth, bool budgetHitIncomplete) {
    return d0HasRunTruth && !budgetHitIncomplete;
}

bool EbLivePlanContracts::waveIwSkipLaterBandForStarve(bool d0DeadWithTruth,
                                                       bool prepareHeavyStillOk) {
    return d0DeadWithTruth && !prepareHeavyStillOk;
}

int EbLivePlanContracts::waveIwChooseDelay(bool d0ObservationSafe, int earliestLaterSafeDelay) {
    if (d0ObservationSafe)
        return 0;
    if (earliestLaterSafeDelay > 0)
        return earliestLaterSafeDelay;
    return -1;
}

int EbLivePlanContracts::waveIwChooseDelayByScore(bool d0ObservationSafe,
                                                  int bestScoreLaterSafeDelay,
                                                  int earliestLaterSafeDelay) {
    if (d0ObservationSafe)
        return 0;
    if (bestScoreLaterSafeDelay > 0)
        return bestScoreLaterSafeDelay;
    if (earliestLaterSafeDelay > 0)
        return earliestLaterSafeDelay;
    return -1;
}

bool EbLivePlanContracts::continuousIwRanksByEndX() {
    return true;
}

bool EbLivePlanContracts::waveIwBaselineCoastAllowed(bool inputWindow, bool continuousControl,
                                                     bool pressDown) {
    if (!pressDown)
        return false;
    if (!inputWindow)
        return true;
    return continuousControl;
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

int EbLivePlanContracts::adaptiveHonestAliveMinTicks(int screenQuantum, int knownDeathTick,
                                                     int modelCap) {
    int minTicks = std::max(1, screenQuantum);
    if (knownDeathTick > 0) {
        minTicks = std::max(minTicks, knownDeathTick);
    }
    if (modelCap > 0) {
        minTicks = std::min(minTicks, modelCap);
    }
    return minTicks;
}

bool EbLivePlanContracts::adaptiveSeedCountsAlive(int ticksRan, int screenQuantum,
                                                  int knownDeathTick, int modelCap) {
    if (ticksRan <= 0) {
        return false;
    }
    return ticksRan >= adaptiveHonestAliveMinTicks(screenQuantum, knownDeathTick, modelCap);
}

int EbLivePlanContracts::discreteIwObservationFloor(int modelCap) {
    return std::max(1, modelCap);
}

bool EbLivePlanContracts::d0AtProveWall(bool d0Alive, bool d0Landed, int d0TicksRan,
                                        int proveFloorTicks) {
    if (!d0Alive || d0Landed || d0TicksRan <= 0)
        return false;
    return d0TicksRan >= std::max(1, proveFloorTicks);
}

bool EbLivePlanContracts::horizonProbeBlocksPassD0Safe(bool probeEnabled, bool d0AtWall,
                                                       bool anyLaterAlive) {
    return probeEnabled && d0AtWall && anyLaterAlive;
}

bool EbLivePlanContracts::microWindowPreferLater(bool microEnabled, int limitTicks,
                                                 float laterEndX, float d0EndX) {
    if (!microEnabled || limitTicks <= 0 || limitTicks > kMicroWindowMaxAssistTicks)
        return false;
    return laterEndX > d0EndX + 1e-3f;
}

bool EbLivePlanContracts::depthTruncatedAlive(bool died, bool unknown, int ticksRan,
                                              int honestFloorTicks) {
    if (died || unknown || ticksRan <= 0) {
        return false;
    }
    int const floor = std::max(1, honestFloorTicks);
    return ticksRan < floor;
}

bool EbLivePlanContracts::fairDepthPreferCand(int bestTicks, int bestBin, float bestScore,
                                              int candTicks, int candBin, float candScore,
                                              int depthFloor) {
    int const floor = std::max(1, depthFloor);
    bool const bestDeep = bestTicks >= floor;
    bool const candDeep = candTicks >= floor;
    if (candDeep == bestDeep) {
        if (candBin != bestBin) {
            return candBin > bestBin;
        }
        return candScore > bestScore;
    }
    // Unequal depth: ignore surviveBin. Shallow needs a clear score gain; deeper
    // proof always outranks a quantum-only cand.
    if (!candDeep && bestDeep) {
        return (candScore - bestScore) > kLandedScoreEps;
    }
    return true;
}

bool EbLivePlanContracts::hopelessBlockedByDepthTruncation(bool anyDepthTruncatedAlive,
                                                           bool anyObservationSafeDelay) {
    (void)anyObservationSafeDelay;
    return anyDepthTruncatedAlive;
}

bool EbLivePlanContracts::hopelessBlockedByLivingLater(bool d0Died, bool anyLaterAlive) {
    return d0Died && anyLaterAlive;
}

bool EbLivePlanContracts::laterAliveBlocksPassD0Safe(bool laterAutonomousAlive,
                                                     bool laterUnderHorizon, int laterSurviveTicks,
                                                     int d0SurviveTicks, int closeGapTicks,
                                                     float laterEndX, float d0EndX) {
    (void)laterUnderHorizon; // kept in signature for call-site honesty / future soft use
    if (!laterAutonomousAlive)
        return false;
    if (laterSurviveTicks > d0SurviveTicks + std::max(0, closeGapTicks))
        return true;
    if ((laterEndX - d0EndX) > kLandedScoreEps)
        return true;
    return false;
}

bool EbLivePlanContracts::weakD0SafeNeedsFallthrough(bool anyLaterUnprovenEndXLead) {
    return anyLaterUnprovenEndXLead;
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
    expect(kMidRunWallPollTicks == 4, "mid-run-wall-poll");
    expect(shouldPollMidRunWall(0) && shouldPollMidRunWall(4) && !shouldPollMidRunWall(1),
           "mid-run-wall-poll-ticks");
    expect(midRunWallExceeded(8000, 8000) && !midRunWallExceeded(8000, 7999) &&
               !midRunWallExceeded(0, 99999),
           "mid-run-wall-exceeded");
    expect(kPrepareMinRemUs == 1500, "prepare-min-rem");
    expect(prepareReserveUs(0, true) == 0 && prepareReserveUs(8170, true) == 4085 &&
               prepareReserveUs(8170, false) == kPrepareMinRemUs,
           "prepare-reserve");
    expect(prepareHeavyWorkAllowed(5000, 4085, false) &&
               !prepareHeavyWorkAllowed(4000, 4085, false) &&
               prepareHeavyWorkAllowed(0, 4085, true),
           "prepare-heavy-allowed");
    expect(kLiveTwTightenTrackFloor == 200, "live-tw-track-floor");
    expect(liveTwApplySkippedForPlanWall(true) && !liveTwApplySkippedForPlanWall(false),
           "live-tw-wall-skip");
    expect(liveTwApplyMaxPerTick(942, false, 96) == 48 &&
               liveTwApplyMaxPerTick(942, true, 96) == 96 &&
               liveTwApplyMaxPerTick(50, false, 96) == 96,
           "live-tw-max-per-tick");
    expect(liveTwApplyBudgetUs(942, false, 350) == 175 && liveTwApplyBudgetUs(942, true, 350) == 350,
           "live-tw-budget-us");
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
    expect(intendedFallthroughClassicBudgetUs(0, 100) == 0, "ap-l5-ft-rem-no-stack");
    expect(intendedFallthroughClassicBudgetUs(900, 50) == 900, "ft-rem-intended-keep");
    expect(intendedLiveSoftWallFloorUs(0) == 0, "ap-l5-soft-wall-unlimited");
    expect(intendedLiveSoftWallFloorUs(2500) == kLiveSoftWallFloorUs, "ap-l5-soft-wall-floor");
    expect(intendedLiveSoftWallFloorUs(8170) == 8170, "ap-l5-soft-wall-keep");
    expect(adaptiveShellBudgetUs(8170, kFallthroughRemUsFloor) == 6170, "ap-l5-shell-wall");
    expect(adaptiveShellBudgetUs(1500, kFallthroughRemUsFloor) == 1, "ap-l5-shell-tiny");
    expect(adaptiveShellBudgetUs(0, kFallthroughRemUsFloor) == 0, "ap-l5-shell-unlimited");
    expect(adaptiveFallthroughReserveUs(8170, false) == kFallthroughRemUsFloor,
           "ap-l5-ft-reserve-cont");
    expect(adaptiveFallthroughReserveUs(8170, true) == 4085, "ap-l5-ft-reserve-discrete");
    expect(adaptiveShellBudgetUs(8170, adaptiveFallthroughReserveUs(8170, true)) == 4085,
           "ap-l5-shell-discrete");
    expect(adaptiveFallthroughSeedsObservationCleared(false), "ap-l5-seed-cube");
    expect(!adaptiveFallthroughSeedsObservationCleared(true), "ap-l5-seed-ball");

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

    expect(kWaveHorizonFloor == 32, "wave-horizon-floor");
    expect(waveShortPlanCap(8) == std::max(32, 8 + 16), "wave-short-plan");
    expect(waveShortPlanCap(20) == 36, "wave-short-plan-grow");
    expect(currentContinuousIwCap(12) == 12, "cur-iw-cap-limit");
    expect(intendedContinuousIwCap(12, waveShortPlanCap(12)) == 32, "int-iw-cap-floor");
    expect(intendedContinuousIwCap(40, waveShortPlanCap(24)) == 40, "int-iw-cap-look-wins");

    expect(waveIwHelpOnlyPass(true, true, true), "wave-iw-help-only");
    expect(!waveIwHelpOnlyPass(true, true, false), "wave-iw-help-need-d0");
    expect(!waveIwHelpOnlyPass(false, true, true), "wave-iw-help-wave-only");
    expect(waveIwChooseDelay(true, 5) == 0, "wave-iw-choose-help");
    expect(waveIwChooseDelay(false, 3) == 3, "wave-iw-choose-earliest");
    expect(waveIwChooseDelay(false, -1) == -1, "wave-iw-choose-none");
    expect(waveIwChooseDelayByScore(true, 7, 3) == 0, "ap5-wave-help");
    expect(waveIwChooseDelayByScore(false, 7, 3) == 7, "ap5-wave-score");
    expect(waveIwChooseDelayByScore(false, -1, 3) == 3, "ap5-wave-fallback");
    expect(continuousIwRanksByEndX(), "ap5-continuous-endx");
    expect(waveIwProximityPass(true, true, false, true, true, false), "wave-iw-prox-pass");
    expect(!waveIwProximityPass(true, true, true, true, true, false), "wave-iw-prox-need-alive-d0");
    expect(!waveIwProximityPass(true, true, false, true, true, true), "wave-iw-prox-orb-blocks");
    expect(waveIwFutilePass(true, true, false, false, false, 0, 3.f, false), "wave-iw-futile");
    expect(!waveIwFutilePass(true, true, true, false, false, 0, 3.f, false), "wave-iw-futile-save");
    expect(!waveIwFutilePass(true, true, false, false, false, 1, 3.f, false), "wave-iw-futile-bin");
    expect(waveIwD0HasRunTruth(true, 12) && !waveIwD0HasRunTruth(true, 0) &&
               !waveIwD0HasRunTruth(false, 12),
           "wave-iw-d0-truth");
    expect(waveIwFutileHonest(true, false) && !waveIwFutileHonest(true, true) &&
               !waveIwFutileHonest(false, false),
           "wave-iw-futile-honest");
    expect(waveIwSkipLaterBandForStarve(true, false) && !waveIwSkipLaterBandForStarve(true, true) &&
               !waveIwSkipLaterBandForStarve(false, false),
           "wave-iw-skip-later-starve");
    expect(waveIwBaselineCoastAllowed(true, true, true), "wave-iw-baseline-coast");
    expect(!waveIwBaselineCoastAllowed(true, false, true), "wave-iw-baseline-discrete-skip");
    expect(waveIwBaselineCoastAllowed(false, false, true), "wave-iw-baseline-classic");

    expect(currentOrbWindowScanRuns(true, false), "orb-scan-classic");
    expect(!currentOrbWindowScanRuns(true, true), "orb-scan-iw-skipped-today");
    expect(intendedSpatialOrbHintAllowed(true), "orb-hint-iw-intended");
    expect(!intendedSpatialOrbHintAllowed(false), "orb-hint-off");
    expect(intendedOrbWindowScanAllowed(true, true, true), "orb-scan-iw-intended");
    expect(!intendedOrbWindowScanAllowed(true, true, false), "orb-scan-budget");

    // AP10 — short Adaptive seed must not beat a known later death.
    expect(!adaptiveSeedCountsAlive(44, 24, 54), "adaptive-seed-before-d0-death-rejected");
    expect(adaptiveSeedCountsAlive(54, 24, 54), "adaptive-seed-covers-d0-death");
    expect(adaptiveSeedCountsAlive(24, 24, -1), "adaptive-seed-quantum-no-death");
    expect(!adaptiveSeedCountsAlive(23, 24, -1), "adaptive-seed-below-quantum");
    expect(adaptiveHonestAliveMinTicks(24, 54, 40) == 40, "adaptive-seed-cap-clamp");
    expect(adaptiveHonestAliveMinTicks(24, 54, 100) == 54, "adaptive-seed-death-floor");

    // Assist Pack AP1 — fair depth (A4-style: shallow Alive ≠ not-viable / Hopeless).
    expect(depthTruncatedAlive(false, false, 18, 54), "depth-trunc-shallow");
    expect(!depthTruncatedAlive(false, false, 54, 54), "depth-trunc-covered");
    expect(!depthTruncatedAlive(true, false, 18, 54), "depth-trunc-dead");
    expect(!depthTruncatedAlive(false, true, 18, 54), "depth-trunc-unknown");
    // Deep d0 bin3 vs quantum later bin0: later wins only with clear score gain.
    expect(!fairDepthPreferCand(96, 3, 100.f, 18, 0, 103.f, 24), "fair-depth-no-bin-steal");
    expect(fairDepthPreferCand(96, 3, 100.f, 18, 0, 109.f, 24), "fair-depth-score-gain");
    expect(fairDepthPreferCand(18, 0, 100.f, 96, 3, 99.f, 24), "fair-depth-deeper-wins");
    expect(fairDepthPreferCand(18, 0, 100.f, 20, 1, 101.f, 24), "fair-depth-both-shallow-bin");
    expect(hopelessBlockedByDepthTruncation(true, false), "hopeless-blocked-trunc");
    expect(!hopelessBlockedByDepthTruncation(false, false), "hopeless-ok-empty");
    // Assist Pack AP4 — A4 living later vs A7 uniform lethal.
    expect(hopelessBlockedByLivingLater(true, true), "ap4-a4-blocked");
    expect(!hopelessBlockedByLivingLater(true, false), "ap4-a7-allowed");
    expect(!hopelessBlockedByLivingLater(false, true), "ap4-d0-alive-n/a");

    // Assist Pack AP3 — only survive/endX lead blocks (not bare under-horizon).
    expect(!laterAliveBlocksPassD0Safe(true, true, 18, 96, 8, 100.f, 100.f),
           "ap3-under-horizon-alone-ok");
    expect(laterAliveBlocksPassD0Safe(true, true, 18, 96, 8, 109.f, 100.f), "ap3-under-horizon-endx");
    expect(laterAliveBlocksPassD0Safe(true, false, 110, 96, 8, 100.f, 100.f), "ap3-survive-lead");
    expect(laterAliveBlocksPassD0Safe(true, false, 96, 96, 8, 109.f, 100.f), "ap3-endx-lead");
    expect(!laterAliveBlocksPassD0Safe(true, false, 96, 96, 8, 103.f, 100.f), "ap3-no-weak-lead");
    expect(!laterAliveBlocksPassD0Safe(false, true, 18, 96, 8, 200.f, 100.f), "ap3-dead-later");
    expect(weakD0SafeNeedsFallthrough(true), "ap3-gate-ft");
    expect(!weakD0SafeNeedsFallthrough(false), "ap3-gate-stick");

    // Assist Pack AP7 — observation floor is model.cap, not limit+cap.
    expect(discreteIwObservationFloor(92) == 92, "ap7-floor-cap");
    expect(discreteIwObservationFloor(0) == 1, "ap7-floor-min");
    expect(discreteIwObservationFloor(158) == 158, "ap7-floor-unclamped");

    // Assist Pack AP3c — horizon probe / micro-window contracts.
    expect(d0AtProveWall(true, false, 96, 92), "ap3c-at-wall");
    expect(!d0AtProveWall(true, true, 96, 92), "ap3c-landed-not-wall");
    expect(!d0AtProveWall(true, false, 40, 92), "ap3c-under-wall");
    expect(horizonProbeBlocksPassD0Safe(true, true, true), "ap3c-probe-block");
    expect(!horizonProbeBlocksPassD0Safe(false, true, true), "ap3c-probe-off");
    expect(!horizonProbeBlocksPassD0Safe(true, true, false), "ap3c-probe-no-later");
    expect(microWindowPreferLater(true, 4, 110.f, 100.f), "ap3c-micro-prefer");
    expect(!microWindowPreferLater(false, 4, 110.f, 100.f), "ap3c-micro-off");
    expect(!microWindowPreferLater(true, 12, 110.f, 100.f), "ap3c-micro-large-window");
    expect(!microWindowPreferLater(true, 4, 100.f, 100.f), "ap3c-micro-no-lead");

    if (fails == 0) {
        report = "LivePlanContracts selfCheck OK";
        return true;
    }
    report = fmt::format("LivePlanContracts selfCheck {} fails: {}", fails, report);
    return false;
}
