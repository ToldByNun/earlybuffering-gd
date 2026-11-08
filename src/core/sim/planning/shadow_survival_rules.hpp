#ifndef SHADOW_SURVIVAL_RULES_HPP
#define SHADOW_SURVIVAL_RULES_HPP

#include "simulation_plan.hpp"

#include "../mode_survival.hpp"
#include "../shadow_simulator.hpp"

class ShadowSurvivalRules {
public:
    static int surviveBinOf(int ticks);
    static int rankingSurviveTicks(ShadowSimulator::RunResult const& r);
    static bool clearsThreatOf(ShadowSimulator::RunResult const& r, ThreatZone const& zone);
    static bool delayViableHonest(bool diedBeforeAction, bool died, bool unknownTrunc,
                                  bool toggleUnknown, bool clearsThreat, bool zoneCleared,
                                  bool zoneValid,
                                  ModeSurvival::Class survival = ModeSurvival::Class::Alive,
                                  bool requireAliveForDelay = false);
    static ModeSurvival::Input survivalInputFrom(ShadowSimulator::RunResult const& r,
                                                 ThreatZone const& zone,
                                                 ShadowSimulator::Model const& model,
                                                 PlayerObject* real);
    static bool runResultDelaySafe(ShadowSimulator::RunResult const& r, ThreatZone const& zone,
                                   ShadowSimulator::Model const& model, PlayerObject* real,
                                   bool requireAliveForDelay = false);
    static bool exitSurvivedHonest(bool died, bool unknown);
    static int resolveRobustK(PlayerObject* real, PlanOptions const& options);

private:
    friend class ShadowPlanInputWindow;
    friend class ShadowPlanThreatProbe;
    friend class ShadowPlanForesight;
    friend class ShadowPlanCandidateSearch;
    friend class ShadowPlanSelection;
    friend class ShadowPlanResult;
    friend class ShadowPlanSearch;
    friend class ShadowPlanContext;

    friend class ShadowRegressionChecks;

    friend class ShadowSequencePlanner;

    friend class ShadowDelayVerifier;

    friend class ThreatZone;
    friend class PlanOptions;
    friend class ActWindow;
    friend class SimPlan;
    friend class InputEvent;
    friend class ShadowSimulator;
    friend class SimulationCandidate;

    inline static constexpr int BRANCH_RELEASES[] = {2, 5, 10, 18};
    inline static constexpr int HOLD_PROBE_RELEASES[] = {0, 1, 2, 3, 5, 8, 10, 14, 18, 24, 32, 40};
    inline static constexpr int HOLD_PROBE_MAX = 8;

    ShadowSurvivalRules() = delete;
};

#endif
