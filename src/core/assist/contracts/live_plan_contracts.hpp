#ifndef LIVE_PLAN_CONTRACTS_HPP
#define LIVE_PLAN_CONTRACTS_HPP

#include <string>

// 1.5.1 AP0/AP1/AP3a — pure Live plan contracts (no PlayLayer).
// Hitch / remUs / peak-death / Wave horizon floor are wired. Landed-rank +
// orb-under-IW + Wave help-only stay current-vs-intended until AP2/AP3b.
// selfCheck locks both tables.
class EbLivePlanContracts {
public:
    // Soft wall hitch ceiling when user sets sim-budget-us=0 (µs).
    static constexpr int kHitchCeilingUs = 8000;
    // Adaptive→classic fallthrough floor when wall spent but ticks remain (µs).
    static constexpr int kFallthroughRemUsFloor = 2000;
    // Matches EbShadowSimulator::kFutileEndXGain — landed noise / tie band.
    static constexpr float kLandedScoreEps = 8.f;
    // Continuous IW Wave short planCap floor (matches AssistHorizon::shortCaps).
    static constexpr int kWaveHorizonFloor = 32;

    // --- Soft wall / hitch -------------------------------------------------

    // Current classic Live: configured<=0 → hitch ceiling; else configured.
    static int currentClassicBudgetUs(int configuredBudgetUs);
    // Current Adaptive Live: configured 0 stays unlimited (0).
    static int currentAdaptiveBudgetUs(int configuredBudgetUs);
    // Intended 1.5.1 (AP1): both paths clamp 0 → hitch ceiling.
    static int intendedLiveBudgetUs(int configuredBudgetUs);

    // Current Adaptive fallthrough when remUs<=0 but ticks remain → 1µs.
    static int currentFallthroughClassicBudgetUs(int remUs, int ticksLeft);
    // Intended AP1: remUs>0 keep remUs; else if ticksLeft>0 → floor.
    static int intendedFallthroughClassicBudgetUs(int remUs, int ticksLeft);

    // --- Landed ranking (input-window ball) --------------------------------

    // Current 1.5.1-dev overshoot: later wins only if score gain > eps.
    static bool currentLandedPreferLater(float bestScore, float candScore, int bestDelay,
                                         int candDelay);
    // Intended AP2 (Eval-aligned): higher score wins; near-tie → earlier delay.
    // Never uses absolute surviveBin (delay-inflated ticksRan under ball landing).
    static bool intendedLandedPreferLater(float bestScore, float candScore, int bestDelay,
                                          int candDelay);

    // --- Peak-death cut ----------------------------------------------------

    // Discrete input-window: survivor then acted death → may stop ascending.
    static bool peakDeathShouldStop(bool inputWindow, bool continuousLike, bool hadSurvivor,
                                    bool diedAfterAction);
    // Contract: leftovers must be Skipped (peak-death-cut), never fake-dead clones.
    static bool peakDeathMarksSkippedNotDead();

    // --- Continuous IW horizon (Wave / ship / swing) -----------------------

    // Current Live IW overwrite: cap = continuousLookAhead(limit) only (=limit).
    static int currentContinuousIwCap(int continuousLookAhead);
    // AssistHorizon Wave short planCap base (before mul/lookAhead clamp).
    static int waveShortPlanCap(int limit);
    // Intended AP3a: max(local look-ahead, short AssistHorizon planCap).
    static int intendedContinuousIwCap(int continuousLookAhead, int shortAssistHorizonPlanCap);

    // --- Orbs under input-window -------------------------------------------

    // Current prepare gate: full orb window scan only when !inputWindow.
    static bool currentOrbWindowScanRuns(bool assistOrbs, bool inputWindow);
    // Intended AP3: cheap spatial hint whenever orbs on (even under IW).
    static bool intendedSpatialOrbHintAllowed(bool assistOrbs);
    // Intended AP3: budget-capped scanOrbWindow allowed under IW when orbs on.
    static bool intendedOrbWindowScanAllowed(bool assistOrbs, bool inputWindow, bool budgetOk);

    static bool selfCheck(std::string& report);

private:
    EbLivePlanContracts() = delete;
};

#endif
