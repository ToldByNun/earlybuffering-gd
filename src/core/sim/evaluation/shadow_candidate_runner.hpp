#ifndef SHADOW_CANDIDATE_RUNNER_HPP
#define SHADOW_CANDIDATE_RUNNER_HPP

#include "../planning/simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class ShadowCandidateRunner {
public:
    static ShadowSimulator::RunResult runOne(ShadowSimulator& simulator, PlayLayer* layer,
                                             PlayerObject* real, bool down, int delay, int limit,
                                             float deltaTime, float firstDt,
                                             ShadowSimulator::Model const& model, int releaseAfter,
                                             ThreatZone const* zone, int* tickBudgetLeft);
    static ShadowSimulator::RunResult runTrajectoryOne(ShadowSimulator& simulator, PlayLayer* layer,
                                                       PlayerObject* real, bool down, int delay,
                                                       int limit, float deltaTime, float firstDt,
                                                       ShadowSimulator::Model const& model,
                                                       int releaseAfter, ThreatZone const* zone,
                                                       int* tickBudgetLeft);
    static ShadowSimulator::RunResult runBaseline(ShadowSimulator& simulator, PlayLayer* layer,
                                                  PlayerObject* real, bool down, float deltaTime,
                                                  float firstDt,
                                                  ShadowSimulator::Model const& model,
                                                  int* tickBudgetLeft);
    static ShadowSimulator::OrbWindow scanOrbWindow(ShadowSimulator& simulator, PlayLayer* layer,
                                                    PlayerObject* real, bool down, int scanTicks,
                                                    float deltaTime, float firstDt,
                                                    int* tickBudgetLeft, bool holdJump);

private:
    ShadowCandidateRunner() = delete;
};

#endif
