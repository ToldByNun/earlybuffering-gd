#ifndef LIVE_STAGE_PARITY_HPP
#define LIVE_STAGE_PARITY_HPP

#include <string>

// Assist Pack AP-NL2 — Nova Stage/Predictions ↔ EarlyBuffer Live click-timing.
// Pure contracts (no PlayLayer). Stage Off / as-made / held(+N) / mid-tick /
// window legality / µs lookahead shrink. Rollback is out of scope.
class EbLiveStageParity {
public:
    // GD physics 240 TPS; Nova Stage leads are frame floats @ 60 FPS.
    static constexpr int kTicksPerFrame = 4;

    // Nova setting `"stage"` table (IDA off_1808132A0) — research map only.
    enum class NovaStage : int {
        Off = 0,
        Stage1 = 1,
        Stage2 = 2,
        Stage3 = 3,
        Extreme = 4,
        Custom = 5
    };

    // Live decision vocabulary matching Nova Stage logs ("in as made" / "held").
    enum class DecisionKind : int { PassThrough = 0, AsMade = 1, Held = 2 };

    // --- Stage → EB tick windows -------------------------------------------

    // Table lead (frames) for all-modes / wave / ship. Custom → -1 (use window-*).
    // Extreme uses RE table floats 8/5/5 (UI string "20 frames…" is mismatched — O).
    static int novaStageLeadFramesAll(NovaStage stage);
    static int novaStageLeadFramesWave(NovaStage stage);
    static int novaStageLeadFramesShip(NovaStage stage);

    static int framesToTicks(int frames);
    static int novaStageTicksAll(NovaStage stage);
    static int novaStageTicksWave(NovaStage stage);
    static int novaStageTicksShip(NovaStage stage);

    // EB Stage-Off equivalent: assist disabled or zero tick window → raw input.
    static bool stageOffEquivalent(bool assistEnabled, int limitTicks);

    // --- Decision vocabulary -----------------------------------------------

    // delay>0 → Held; delay==0 with buffering on → AsMade; Stage-Off → PassThrough.
    static DecisionKind classifyDecision(bool stageOff, int delayTicks);
    static char const* decisionLabel(DecisionKind kind);

    // --- Window legality / Delay honesty -----------------------------------

    // Rescue window: 0 < delay ≤ limit and fireTick in [press, press+limit].
    static bool windowLegal(int delay, int limit, int pressTick, int fireTick);

    // Alive-only Delay: unknown / soft-trust partial never verify; budgetHit alone
    // does not invent a Delay when the chosen cand was never viable.
    static bool delayArmVerified(bool candViable, bool budgetHit, bool unknown,
                                 bool softTrustPartial = false);

    // --- Mid-tick / CBF (G7 / NL-R6) ----------------------------------------

    // firePhase in (0,1) means inject on a mid-tick prefix (CBF-compatible).
    static bool midTickPhaseActive(float firePhase);
    // Prefix inject only on the armed fireTick while phase is mid-tick.
    static bool midTickPrefixAllowed(float firePhase, int attemptTick, int fireTick);

    // --- Nova-style lookahead shrink under µs wall -------------------------

    // When remUs < half of fullBudgetUs, shrink the scan limit linearly.
    // remUs<=0 → 0 (caller PassBudget / Stage-Off path). fullBudgetUs<=0 → no shrink.
    static int shrinkLimitUnderBudget(int limit, int remUs, int fullBudgetUs);

    static bool selfCheck(std::string& report);

private:
    EbLiveStageParity() = delete;
};

#endif
