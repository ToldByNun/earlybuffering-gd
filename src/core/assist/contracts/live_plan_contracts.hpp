#ifndef LIVE_PLAN_CONTRACTS_HPP
#define LIVE_PLAN_CONTRACTS_HPP

#include <string>

// 1.5.1 AP0–AP4 — pure Live plan contracts (no PlayLayer).
// Hitch / remUs / peak-death / Wave horizon + help-only/prox / orb-under-IW
// are wired. Landed-rank stays current-vs-intended until AP2.
// selfCheck locks both tables.
class EbLivePlanContracts {
public:
    // Soft wall hitch ceiling when user sets sim-budget-us=0 (µs).
    static constexpr int kHitchCeilingUs = 8000;
    // Adaptive→classic fallthrough floor when wall spent but ticks remain (µs).
    static constexpr int kFallthroughRemUsFloor = 2000;
    // Assist Pack AP-L1: mid-run soft-wall poll interval inside one candidate sim.
    static constexpr int kMidRunWallPollTicks = 4;
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

    // Assist Pack AP-L1: poll soft wall on tickIndex 0,4,8,… inside one run.
    static bool shouldPollMidRunWall(int tickIndex);
    // True when budgetUs>0 and elapsedUs has reached the soft wall.
    static bool midRunWallExceeded(int budgetUs, long long elapsedUs);

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

    // --- Wave IW help-only / anti-phase (AP3b) ------------------------------

    // d0 observation-safe / Alive → Pass (never Delay just to pad endX).
    static bool waveIwHelpOnlyPass(bool isWave, bool pressDown, bool d0ObservationSafe);
    // Alive d0 + hazard proximity + no orb → PassWaveProximity (not phase Delay).
    static bool waveIwProximityPass(bool isWave, bool pressDown, bool d0Died, bool nearHazard,
                                    bool noOrbTarget, bool foresightOrbPad);
    // No Alive save from a dying d0 / no useful bin·gain → PassWaveFutile.
    static bool waveIwFutilePass(bool isWave, bool pressDown, bool anyAliveSaveFromD0Death,
                                 bool idleHoldsEscape, bool orbIntent, int bestSurviveBin,
                                 float bestEndXGain, bool foresightOrbPad);
    // Choose armed delay: help-only → 0; else earliest later safe (−1 = none).
    static int waveIwChooseDelay(bool d0ObservationSafe, int earliestLaterSafeDelay);
    // Assist Pack AP5: when d0 is unsafe, prefer the score-best later safe delay
    // (endX − λ·delay) over earliest-only. Help-only still returns 0.
    static int waveIwChooseDelayByScore(bool d0ObservationSafe, int bestScoreLaterSafeDelay,
                                        int earliestLaterSafeDelay);

    // Assist Pack AP5: continuous IW (ship/swing) ranks full survivor band by
    // endX−λ / fair depth — not first-safe early exit.
    static bool continuousIwRanksByEndX();
    // Cheap coast baseline under continuous IW (idleSafer) is allowed.
    static bool waveIwBaselineCoastAllowed(bool inputWindow, bool continuousControl, bool pressDown);

    // --- Orbs under input-window -------------------------------------------

    // Current prepare gate: full orb window scan only when !inputWindow.
    static bool currentOrbWindowScanRuns(bool assistOrbs, bool inputWindow);
    // Intended AP3: cheap spatial hint whenever orbs on (even under IW).
    static bool intendedSpatialOrbHintAllowed(bool assistOrbs);
    // Intended AP3: budget-capped scanOrbWindow allowed under IW when orbs on.
    static bool intendedOrbWindowScanAllowed(bool assistOrbs, bool inputWindow, bool budgetOk);

    // --- Adaptive→classic seed honesty (AP10) ------------------------------

    // Min ticks a later Alive cand must have simulated to honestly beat a known
    // death at knownDeathTick. screenQuantum = Adaptive screen floor. modelCap>0
    // clamps the requirement (cannot demand past the look-ahead wall).
    // knownDeathTick<=0 → quantum alone.
    static int adaptiveHonestAliveMinTicks(int screenQuantum, int knownDeathTick,
                                           int modelCap = 0);
    // Seeded / screen Alive counts only when ticksRan covers the honest floor.
    static bool adaptiveSeedCountsAlive(int ticksRan, int screenQuantum, int knownDeathTick,
                                        int modelCap = 0);

    // --- Fair depth before ranking / Hopeless (Assist Pack AP1) -------------

    // Assist Pack AP7: discrete IW observation floor is the prove horizon
    // (model.cap), not limit+cap. limit+cap is often unreachable after the
    // Adaptive 96−limit clamp / budget truncation (Silent Hoveropolis cube).
    static int discreteIwObservationFloor(int modelCap);

    // Assist Pack AP3c — Probe Past Horizon / Micro-Window (each setting-gated).
    static constexpr int kPostHorizonProbeTicks = 24;
    static constexpr int kMicroWindowMaxAssistTicks = 8; // ~2 frames @ 240 TPS

    // d0 Alive only by hitting the prove wall (not landed) — death may sit past it.
    static bool d0AtProveWall(bool d0Alive, bool d0Landed, int d0TicksRan, int proveFloorTicks);
    // When probe enabled: wall-safe d0 + any later Alive → do not sticky PassD0Safe.
    static bool horizonProbeBlocksPassD0Safe(bool probeEnabled, bool d0AtWall, bool anyLaterAlive);
    // When micro enabled and limit is tiny: any positive endX lead may beat Pass-now.
    static bool microWindowPreferLater(bool microEnabled, int limitTicks, float laterEndX,
                                       float d0EndX);

    // Alive run stopped before the honest observation floor (budget / quantum),
    // not a real death. Callers must pass !died && !unknown already when useful;
    // this helper still guards those flags.
    static bool depthTruncatedAlive(bool died, bool unknown, int ticksRan, int honestFloorTicks);

    // surviveBin may only decide when both sides reached depthFloor ticks.
    // Unequal depth: shallow never loses on bin alone; needs clear score gain
    // (kLandedScoreEps) to beat a deeper cand. Deeper cand always beats shallow.
    static bool fairDepthPreferCand(int bestTicks, int bestBin, float bestScore, int candTicks,
                                    int candBin, float candScore, int depthFloor);

    // Hopeless is dishonest when any Alive (incl. depth-truncated) still exists.
    // PassBudget / extend instead of PassHopeless.
    static bool hopelessBlockedByDepthTruncation(bool anyDepthTruncatedAlive,
                                                 bool anyObservationSafeDelay);

    // Assist Pack AP4: PassHopeless only when d0 is dead AND no later Alive exists
    // (shallow counts). Living later grid → PassBudget / fallthrough, never Hopeless.
    // Uniform same-crash death (A7) still allows Hopeless when anyLaterAlive=false.
    static bool hopelessBlockedByLivingLater(bool d0Died, bool anyLaterAlive);

    // Assist Pack AP3 (tightened): later Alive blocks terminal PassD0Safe only with a
    // clear surviveTicks lead or endX lead — NOT merely because it is under-horizon
    // (blanket under-horizon starved Delay arms: research_20261113_023017).
    static bool laterAliveBlocksPassD0Safe(bool laterAutonomousAlive, bool laterUnderHorizon,
                                           int laterSurviveTicks, int d0SurviveTicks,
                                           int closeGapTicks, float laterEndX, float d0EndX);

    // Gate: proven d0 + later Living that is unproven AND endX-leading → PassBudget.
    // Unproven later without an endX lead must not demote every PassD0Safe.
    static bool weakD0SafeNeedsFallthrough(bool anyLaterUnprovenEndXLead);

    static bool selfCheck(std::string& report);

private:
    EbLivePlanContracts() = delete;
};

#endif
