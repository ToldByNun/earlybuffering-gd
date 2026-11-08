#ifndef SHADOW_PLAN_CONTEXT_HPP
#define SHADOW_PLAN_CONTEXT_HPP

#include "../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../simulation_candidate.hpp"
#include "../simulation_plan.hpp"
#include <vector>

#include "../../shadow_simulator.hpp"
#include <chrono>

// Per-call state shared by search phases. Capture cleanup remains scoped to run().
class ShadowPlanContext {
public:
    ShadowPlanContext(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                      int limit, float deltaTime, float firstDt, PlanOptions const& options,
                      SimPlan& plan);

private:
    friend class ShadowPlanInputWindow;
    friend class ShadowPlanThreatProbe;
    friend class ShadowPlanForesight;
    friend class ShadowPlanCandidateSearch;
    friend class ShadowPlanSelection;
    friend class ShadowPlanResult;
    friend class ShadowPlanSearch;
    bool wallExceeded() const;
    bool linkedDual() const;
    ShadowSimulator::RunResult runEngine(PlayerObject* who, int delay, int releaseAfter,
                                         ThreatZone const* zonePointer);
    void publishLedger(int chosenDelay, bool doPassFlag, DevRejectLedger::EarlyExit early);
    SimulationCandidate evalDelay(int delayValue, int releaseAfter);
    void finalizeViable(SimulationCandidate& candidate, int releaseAfter);
    bool jitterOk(int delay, int releaseAfter);

    ShadowSimulator& simulator;
    PlayLayer* layer;
    PlayerObject* real;
    bool down;
    int limit;
    float deltaTime;
    float firstDt;
    PlanOptions const& options;
    SimPlan& plan;
    ShadowSimulator::Model model;
    int robustK;
    float latencyWeight;
    int tickBudget;
    std::chrono::steady_clock::time_point wall0;
    bool useTrajEngine;
    bool sawToggleUnknown = false;
    bool hadToggleTargets = false;
    bool inputWindow = false;
    bool orbAssist = false;
    ShadowSimulator::OrbWindow orbHint{};
    bool spatialHint = false;
    ThreatZone zone{};
    std::vector<SimulationCandidate> cands;
    std::vector<char> candReady;
    int ledgerActMin = -1;
    int ledgerActMax = -1;
    bool ledgerOrbIntent = false;
    bool ledgerPreferEarlyExit = false;
    int minSurviveLong = 0;
    bool requireAlive = false;
    int scoreHorizon = 0;
    int preferLo = -1;
    int preferHi = -1;
    bool hasPrefer = false;
    int chosen = -1;
    bool doPass = false;
    bool orbIntent = false;
};

#endif
