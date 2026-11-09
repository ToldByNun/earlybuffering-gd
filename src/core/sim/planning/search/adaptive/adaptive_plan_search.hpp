#ifndef ADAPTIVE_PLAN_SEARCH_HPP
#define ADAPTIVE_PLAN_SEARCH_HPP

#include "../../simulation_plan.hpp"
#include "../../../shadow_simulator.hpp"
#include <string>

// Adaptive Prediction Planner entry (AP4 A/B + AP5 Phase C).
// !adaptivePlan → classic ShadowPlanSearch.
// Early Pass (hopeless / budget / d0-safe / unknown) returns immediately.
// Otherwise fall through to classic with remaining Dynamic Budget (AP6 DecisionGate
// will own Assist arming without classic fallthrough).
class EbAdaptivePlanSearch {
public:
    static EbSimPlan run(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                         int limit, float deltaTime, float firstDt, EbPlanOptions const& options);

    // Last shell overlay bit (`adap screen=N surv=M ver=K [ft|pass]`), empty if unused.
    static std::string overlayBit();

    static bool selfCheck(std::string& report);

private:
    class ShellStats {
    public:
        bool used = false;
        bool fellThrough = false;
        bool earlyPass = false;
        int screened = 0;
        int survivors = 0;
        int verifyGrants = 0;

    private:
    };

    static ShellStats s_lastStats;

    static void noteStats(ShellStats const& stats);

    EbAdaptivePlanSearch() = delete;
};

#endif
