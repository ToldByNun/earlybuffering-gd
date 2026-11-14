#ifndef LIVE_PLAN_CONTRACTS_HPP
#define LIVE_PLAN_CONTRACTS_HPP

#include <cstddef>
#include <string>

// 1.5.1 AP0–AP4 — pure Live plan contracts (no PlayLayer).
// Hitch / remUs / peak-death / Wave horizon + help-only/prox / orb-under-IW
// are wired. Landed-rank stays current-vs-intended until AP2.
// selfCheck locks both tables.
class EbLivePlanContracts {
public:
    // Soft wall hitch ceiling when user sets sim-budget-us=0 (µs).
    static constexpr int kHitchCeilingUs = 8000;
    // Adaptive shell / continuous fallthrough reserve floor (µs) — not a stack
    // past sim-budget-us once the shared press wall is spent (AP-L5).
    static constexpr int kFallthroughRemUsFloor = 2000;
    // Assist Pack AP-L1: mid-run soft-wall poll interval inside one candidate sim.
    static constexpr int kMidRunWallPollTicks = 4;
    // Assist Pack AP-L2: skip prepare baseline/orb when remUs below this floor.
    static constexpr int kPrepareMinRemUs = 1500;
    // Assist Pack AP-L3: live TW apply half-caps when ready tracks ≥ this.
    static constexpr int kLiveTwTightenTrackFloor = 200;
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

    // Assist Pack AP-L2: µs to keep for IW after prepare. budgetUs<=0 → 0 (unlimited).
    // Continuous Live IW reserves max(kPrepareMinRemUs, budgetUs/2); else min floor.
    static int prepareReserveUs(int budgetUs, bool continuousInputWindow);
    // Heavy prepare (baseline / orb scan) allowed when unlimited or remUs >= reserve.
    static bool prepareHeavyWorkAllowed(int remUs, int reserveUs, bool wallUnlimited);

    // Assist Pack AP-L3: skip TW apply for this physics tick when plan soft wall is spent.
    static bool liveTwApplySkippedForPlanWall(bool planWallExceeded);
    // Live (non-bake) half-cap when trackCount ≥ floor; bake always keeps fullCap.
    static size_t liveTwApplyMaxPerTick(size_t trackCount, bool bakeOffline, size_t fullCap);
    static int liveTwApplyBudgetUs(size_t trackCount, bool bakeOffline, int fullBudgetUs);

    // Current Adaptive fallthrough when remUs<=0 but ticks remain → 1µs.
    static int currentFallthroughClassicBudgetUs(int remUs, int ticksLeft);
    // Assist Pack AP-L5: remUs>0 keep remUs; remUs≤0 → 0 (no floor stack past
    // the shared press wall). Shell reserve is adaptiveFallthroughReserveUs.
    static int intendedFallthroughClassicBudgetUs(int remUs, int ticksLeft);

    // AP-P05: preserve explicit low Live budgets.
    // budgetUs<=0 (unlimited / hitch-clamp input) unchanged.
    static int intendedLiveSoftWallFloorUs(int budgetUs);

    // Assist Pack AP-L7: lag notification gate — feel hitch (planUs ≥ hitch
    // ceiling) or budgetHit; independent of configured sim-budget-us.
    static bool lagWarningShouldNotify(bool enabled, int planUs, bool budgetHit);

    // Assist Pack AP-L5: Adaptive soft wall leaves fallthroughReserveUs on the
    // shared press budget (no remUs floor stacked past sim-budget-us).
    static int adaptiveShellBudgetUs(int fullBudgetUs, int fallthroughReserveUs);
    // µs reserved for classic fallthrough before Adaptive spends the press wall.
    // Continuous: kFallthroughRemUsFloor. Discrete IW (ball/cube): ≥ half budget
    // (Hoveropolis classic ball IW median ~3.8ms @ simUs=8170).
    static int adaptiveFallthroughReserveUs(int fullBudgetUs, bool discreteInputWindow);
    // Observation-cleared Alive seeds skip on ball — sticky PassD0Safe vs classic
    // stopAtBallLanding Delay ranking (Hoveropolis research_20261113_192707).
    static bool adaptiveFallthroughSeedsObservationCleared(bool isBall);
    // Assist Pack AP-M6: Adaptive shell wall / tick miss must not stamp classic
    // budgetHit when fallthrough still has remUs on the shared press wall.
    static bool adaptiveShellHitPoisonsFallthrough(int fallthroughRemUs);
    // Discrete Live IW: after screen with zero Autonomous Alive → abort Adaptive
    // immediately and gift remaining remUs to classic fallthrough.
    static bool adaptiveAbortToFallthroughAfterScreen(bool discreteLiveIw, int survivors,
                                                      int screened);

    // Assist Pack AP-M7 — Continuous Wave Adaptive with zero Autonomous survivors
    // must still run classic continuous IW (Grief: Adaptive overspent the shared
    // wall before any screen progress; River already finds Delay via hybrid).
    static bool adaptiveWaveZeroSurvivorsNeedsClassicIw(bool isWave, bool continuousControl,
                                                        int survivors);
    // Gift fallthroughReserveUs when Wave needs classic IW but shared remUs is gone.
    static int adaptiveWaveFallthroughGiftUs(bool needsClassicIw, int remUs,
                                             int fallthroughReserveUs);
    // Fresh classic soft wall for the gift (reusePlanWall would already be spent).
    static bool adaptiveWaveFallthroughUsesFreshWall(bool needsClassicIw, int remUs);
    // TW-heavy Wave: shrink Adaptive screen quantum so shell leaves remUs for IW.
    static int adaptiveWaveScreenTicksForTw(int adaptiveScreenTicks, size_t twTrackCount);

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
    static constexpr bool ballOrbFallbackAllowed(bool isBall, bool orbAssist, bool hasStableLanding,
                                                 bool groundSearchComplete) {
        return isBall && orbAssist && !hasStableLanding && groundSearchComplete;
    }

    // UFO timing windows can reopen after several equal ceiling collisions.
    static constexpr bool repeatedDeathCutAllowed(bool isUfo, bool repeatedDeath) {
        return !isUfo && repeatedDeath;
    }
    static constexpr int ufoRescueProbe(int limit, int index) {
        if (limit <= 1 || index < 0 || index > 2)
            return -1;
        if (index == 0)
            return limit < 4 ? 1 : limit / 4;
        if (index == 1)
            return limit / 2;
        return limit;
    }

    // AP07: reach middle/late Cube windows before ascending dead prefixes
    // consume the wall. The caller still verifies every proposed delay.
    static constexpr int cubeRescueProbe(int limit, int index) {
        if (limit <= 1 || index < 0 || index > 2)
            return -1;
        if (index == 0)
            return limit / 2;
        if (index == 1)
            return limit - limit / 4;
        return limit;
    }

    static constexpr bool waveRecoveryProbeAllowed(int baselineDeathTick, int delay,
                                                   int nextPressTick) {
        return baselineDeathTick >= 0 && delay > 0 && nextPressTick > baselineDeathTick &&
               nextPressTick >= delay + 8;
    }

    static constexpr bool waveTurnHasMargin(bool previousSafe, bool currentSafe, bool nextSafe) {
        return previousSafe && currentSafe && nextSafe;
    }

    // AP05: a short Alive screen or a later collision is not a verified turn.
    static constexpr bool waveTurnVerified(bool died, bool unknown, int ticksRan,
                                           int observationFloor) {
        return !died && !unknown && observationFloor > 0 && ticksRan >= observationFloor;
    }

    static bool waveIwHelpOnlyPass(bool isWave, bool pressDown, bool d0ObservationSafe);
    // Alive d0 + hazard proximity + no orb → PassWaveProximity (not phase Delay).
    static bool waveIwProximityPass(bool isWave, bool pressDown, bool d0Died, bool nearHazard,
                                    bool noOrbTarget, bool foresightOrbPad);
    // No Alive save from a dying d0 / no useful bin·gain / no later progress →
    // PassWaveFutile. Assist Pack AP-M1: anyLaterProgress blocks futile.
    static bool waveIwFutilePass(bool isWave, bool pressDown, bool anyAliveSaveFromD0Death,
                                 bool idleHoldsEscape, bool orbIntent, int bestSurviveBin,
                                 float bestEndXGain, bool foresightOrbPad,
                                 bool anyLaterProgress = false);
    // Assist Pack AP-L4: d0 was actually simulated (not synthetic ledger).
    static bool waveIwD0HasRunTruth(bool candReady0, int d0TicksRan);
    // PassWaveFutile only with d0 run truth and no budget/wall starve mid-band.
    static bool waveIwFutileHonest(bool d0HasRunTruth, bool budgetHitIncomplete);
    // Dying d0 + remUs below continuous prepare reserve → skip later-band scan.
    static bool waveIwSkipLaterBandForStarve(bool d0DeadWithTruth, bool prepareHeavyStillOk);
    // Assist Pack AP-M1: later cand beats dying d0 on death clock / ticks / endX.
    static bool waveIwLaterProgressBeatsD0(bool d0Died, int d0DeathTick, int d0TicksRan,
                                           float d0EndX, bool laterReady, bool laterDied,
                                           int laterDeathTick, int laterTicksRan, float laterEndX);
    // Assist Pack AP-M1: incomplete later band without budgetHit must not claim futile.
    static bool waveIwPreferPassBudgetOverFutile(bool d0HasRunTruth, bool budgetHit,
                                                 int laterReadyCount, int limit);
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
    static bool waveIwBaselineCoastAllowed(bool inputWindow, bool continuousControl,
                                           bool pressDown);

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
    static int adaptiveHonestAliveMinTicks(int screenQuantum, int knownDeathTick, int modelCap = 0);
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
    // Assist Pack AP-L6: run d0 horizon probe after d0 / before the 1..limit band
    // so mid-band wall spend cannot starve AP3c (discrete only; Wave stays AP-L4).
    static bool discreteIwProbeBeforeBand(bool continuous, bool probeEnabled, bool d0AtWall);
    // When micro enabled and limit is tiny: any positive endX lead may beat Pass-now.
    static bool microWindowPreferLater(bool microEnabled, int limitTicks, float laterEndX,
                                       float d0EndX);

    // Assist Pack AP-M3 — Classic discrete: AP-L6 probe can leave d0 Alive at
    // observationFloor+24 while the later band is restored to observationFloor.
    // Probe-inflated endX must not sticky PassD0Safe against floor-reached later
    // Alive (Slaughterhouse cube underselect). Bare under-horizon Alone still
    // does not demote (AP3 tighten preserved).
    static bool discreteIwProbeExtendedD0Alive(bool d0Alive, int d0TicksRan,
                                               int observationFloorTicks, bool ranHorizonProbe);
    static bool discreteIwLivingBandDemotesPassD0Safe(bool discreteIw, bool probeExtendedD0,
                                                      bool laterAliveAtObservationFloor);

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

    // Assist Pack AP-M4 — Ball false Hopeless. Raw run-alive (not viable/surviveBin);
    // ball landed laters count. Incomplete ball band without budgetHit must not
    // seal PassHopeless (prefix/peak cut mid-band → PassBudget).
    static bool discreteIwLaterRunAliveForHopeless(bool died, bool unknown, int ticksRan);
    static bool ballIwIncompleteBandBlocksHopeless(bool isBall, bool budgetHit, int candidatesTried,
                                                   int limit);
    static bool discreteIwHopelessMustFallthrough(bool d0Died, bool anyLaterRunAlive,
                                                  bool anyDepthTruncatedAlive, bool isBall,
                                                  bool budgetHit, int candidatesTried, int limit);

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
