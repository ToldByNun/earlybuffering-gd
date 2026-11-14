#ifndef SHADOW_CANDIDATE_RUNNER_HPP
#define SHADOW_CANDIDATE_RUNNER_HPP

#include "../planning/simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class EbShadowCandidateRunner {
public:
    static EbShadowSimulator::RunResult
    runOne(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down, int delay,
           int limit, float deltaTime, float firstDt, EbShadowSimulator::Model const& model,
           int releaseAfter, EbThreatZone const* zone, int* tickBudgetLeft, float firePhase = 0.f);
    static EbShadowSimulator::RunResult runTrajectoryOne(EbShadowSimulator& simulator, PlayLayer* layer,
                                                       PlayerObject* real, bool down, int delay,
                                                       int limit, float deltaTime, float firstDt,
                                                       EbShadowSimulator::Model const& model,
                                                       int releaseAfter, EbThreatZone const* zone,
                                                       int* tickBudgetLeft);
    static EbShadowSimulator::RunResult runBaseline(EbShadowSimulator& simulator, PlayLayer* layer,
                                                  PlayerObject* real, bool down, float deltaTime,
                                                  float firstDt,
                                                  EbShadowSimulator::Model const& model,
                                                  int* tickBudgetLeft);
    static EbShadowSimulator::OrbWindow scanOrbWindow(EbShadowSimulator& simulator, PlayLayer* layer,
                                                    PlayerObject* real, bool down, int scanTicks,
                                                    float deltaTime, float firstDt,
                                                    int* tickBudgetLeft, bool holdJump);

private:
    EbShadowCandidateRunner() = delete;
};

#endif
