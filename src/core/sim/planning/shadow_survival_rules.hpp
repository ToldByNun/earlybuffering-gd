#ifndef SHADOW_SURVIVAL_RULES_HPP
#define SHADOW_SURVIVAL_RULES_HPP

#include "simulation_plan.hpp"

#include "../mode_survival.hpp"
#include "../shadow_simulator.hpp"

class EbShadowSurvivalRules {
public:
    // Extra ticks past Prediction Horizon when Live requires Alive for Delay.
    // Catches deaths on the tick after the wall (Dump-SP / startpos class).
    static constexpr int kPostCapProveTicks = 8;

    static int surviveBinOf(int ticks);
    static int rankingSurviveTicks(EbShadowSimulator::RunResult const& r);
    static bool clearsThreatOf(EbShadowSimulator::RunResult const& r, EbThreatZone const& zone);
    static bool delayViableHonest(bool diedBeforeAction, bool died, bool unknownTrunc,
                                  bool toggleUnknown, bool clearsThreat, bool zoneCleared,
                                  bool zoneValid,
                                  EbModeSurvival::Class survival = EbModeSurvival::Class::Alive,
                                  bool requireAliveForDelay = false);
    static EbModeSurvival::Input survivalInputFrom(EbShadowSimulator::RunResult const& r,
                                                 EbThreatZone const& zone,
                                                 EbShadowSimulator::Model const& model,
                                                 PlayerObject* real);
    static bool runResultDelaySafe(EbShadowSimulator::RunResult const& r, EbThreatZone const& zone,
                                   EbShadowSimulator::Model const& model, PlayerObject* real,
                                   bool requireAliveForDelay = false);
    static bool exitSurvivedHonest(bool died, bool unknown);
    static int resolveRobustK(PlayerObject* real, EbPlanOptions const& options);
    // ±k must fit the Max Assist window. limit=1 + k=1 made Delay@1 require
    // viable d0 — so cube with a 1-tick window almost never armed a Delay.
    static int clampRobustKToWindow(int robustK, int limit);

private:
    friend class EbShadowPlanInputWindow;
    friend class EbShadowPlanThreatProbe;
    friend class EbShadowPlanForesight;
    friend class EbShadowPlanCandidateSearch;
    friend class EbShadowPlanSelection;
    friend class EbShadowPlanResult;
    friend class EbShadowPlanSearch;
    friend class EbShadowPlanContext;

    friend class EbShadowRegressionChecks;

    friend class EbShadowSequencePlanner;

    friend class EbShadowDelayVerifier;

    friend class EbThreatZone;
    friend class EbPlanOptions;
    friend class EbActWindow;
    friend class EbSimPlan;
    friend class EbInputEvent;
    friend class EbShadowSimulator;
    friend class EbSimulationCandidate;

    inline static constexpr int BRANCH_RELEASES[] = {2, 5, 10, 18};
    inline static constexpr int HOLD_PROBE_RELEASES[] = {0, 1, 2, 3, 5, 8, 10, 14, 18, 24, 32, 40};
    inline static constexpr int HOLD_PROBE_MAX = 8;

    EbShadowSurvivalRules() = delete;
};

#endif
