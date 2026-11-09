#ifndef ADAPTIVE_BUDGET_CONTROLLER_HPP
#define ADAPTIVE_BUDGET_CONTROLLER_HPP

#include "../../simulation_plan.hpp"
#include <string>

// Decides how much Dynamic Plan Tick Budget a press may spend, and how it is
// split across Adaptive phases. Never raises Max Assist ticks or Prediction
// Horizon — only subdivides tickBudget / budgetUs ceilings.
class EbAdaptiveBudgetController {
public:
    enum class Phase {
        Screen = 0, // Phase A/B baseline + candidate screen
        Verify = 1, // Phase C adaptive verification
        Reserve = 2 // Phase D ties / unclear cases
    };

    class Envelope {
    public:
        int allowedTicks = 0; // hard sum ≤ Stage tick ceiling (platform-clamped)
        int budgetUs = 0;     // soft wall; 0 = unlimited (user set sim-budget-us=0)
        int screenTicks = 0;
        int verifyTicks = 0;
        int reserveTicks = 0;
        int screenLeft = 0;
        int verifyLeft = 0;
        int reserveLeft = 0;
        int screenQuantum = 24;
        int verifyQuantum = 16;
        float emaUsPerTick = 0.f;

        int totalLeft() const;
        int phaseLeft(Phase phase) const;
        // Spend up to `ticks` from a phase bucket (and shrink allowed accounting).
        // Returns false if the phase has no remaining ticks.
        bool spend(Phase phase, int ticks);
        int takeQuantum(Phase phase) const;

    private:
    };

    // Pure compute. tickCeiling / usCeiling are already platform-clamped by caller
    // (StageConfig getters). emaUsPerTick ≤ 0 → ignore wall estimate (full tick ceiling).
    // reservePctClamped in 0..40. Screen share of the non-reserve work pool = 30%.
    static Envelope plan(int tickCeiling, int usCeiling, float emaUsPerTick, int screenQuantum,
                         int verifyQuantum, int reservePct);

    // Live helper: opts.tickBudget / budgetUs + Adaptive knobs + SimTickCostMeter EMA.
    static Envelope planFromOptions(EbPlanOptions const& options);

    static bool selfCheck(std::string& report);

private:
    static constexpr int kScreenWorkSharePct = 30; // of (allowed − reserve)
    static constexpr int kMinBucketTicks = 0;
    static constexpr float kMinEmaUs = 0.05f; // ignore absurdly tiny EMA

    EbAdaptiveBudgetController() = delete;
};

#endif
