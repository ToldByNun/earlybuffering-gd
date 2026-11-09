#ifndef SHADOW_PLAN_CONTEXT_HPP
#define SHADOW_PLAN_CONTEXT_HPP

#include "../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../simulation_candidate.hpp"
#include "../simulation_plan.hpp"
#include <vector>

#include "../../shadow_simulator.hpp"
#include <chrono>

// Per-call state shared by search phases. Capture cleanup remains scoped to run().
class EbShadowPlanContext {
public:
    EbShadowPlanContext(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                      int limit, float deltaTime, float firstDt, EbPlanOptions const& options,
                      EbSimPlan& plan);

private:
    friend class EbShadowPlanInputWindow;
    friend class EbShadowPlanThreatProbe;
    friend class EbShadowPlanForesight;
    friend class EbShadowPlanCandidateSearch;
    friend class EbShadowPlanSelection;
    friend class EbShadowPlanResult;
    friend class EbShadowPlanSearch;
    bool wallExceeded() const;
    bool linkedDual() const;
    EbShadowSimulator::RunResult runEngine(PlayerObject* who, int delay, int releaseAfter,
                                         EbThreatZone const* zonePointer);
    void publishLedger(int chosenDelay, bool doPassFlag, EbDevRejectLedger::EarlyExit early);
    EbSimulationCandidate evalDelay(int delayValue, int releaseAfter);
    void finalizeViable(EbSimulationCandidate& candidate, int releaseAfter);
    bool jitterOk(int delay, int releaseAfter);

    EbShadowSimulator& simulator;
    PlayLayer* layer;
    PlayerObject* real;
    bool down;
    int limit;
    float deltaTime;
    float firstDt;
    EbPlanOptions const& options;
    EbSimPlan& plan;
    EbShadowSimulator::Model model;
    int robustK;
    float latencyWeight;
    int tickBudget;
    std::chrono::steady_clock::time_point wall0;
    bool useTrajEngine;
    bool sawToggleUnknown = false;
    bool hadToggleTargets = false;
    bool inputWindow = false;
    bool orbAssist = false;
    EbShadowSimulator::OrbWindow orbHint{};
    bool spatialHint = false;
    EbThreatZone zone{};
    std::vector<EbSimulationCandidate> cands;
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
