#ifndef ADAPTIVE_PLAN_SEARCH_HPP
#define ADAPTIVE_PLAN_SEARCH_HPP

#include "../../simulation_plan.hpp"
#include "../../../shadow_simulator.hpp"
#include <string>

// Adaptive Prediction Planner entry (AP4–AP6).
// !adaptivePlan → classic ShadowPlanSearch.
// A/B screen → C verify → D DecisionGate (honesty).
// Soft wall (budgetUs) does not abort Max Assist screen; incomplete screen /
// Gate PassBudget with no survivors falls through to classic.
class EbAdaptivePlanSearch {
public:
    static EbSimPlan run(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                         int limit, float deltaTime, float firstDt, EbPlanOptions const& options);

    // Last shell overlay bit (`adap screen=N surv=M ver=K [pass|dN]`), empty if unused.
    static std::string overlayBit();

    static bool selfCheck(std::string& report);

private:
    class ShellStats {
    public:
        bool used = false;
        bool earlyPass = false;
        bool gated = false;
        int screened = 0;
        int survivors = 0;
        int verifyGrants = 0;
        int gatedDelay = -1; // ≥0 when Delay armed; 0 may also be PassD0Safe

    private:
    };

    static ShellStats s_lastStats;

    static void noteStats(ShellStats const& stats);

    EbAdaptivePlanSearch() = delete;
};

#endif
