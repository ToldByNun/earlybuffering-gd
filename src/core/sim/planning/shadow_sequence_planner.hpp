#ifndef SHADOW_SEQUENCE_PLANNER_HPP
#define SHADOW_SEQUENCE_PLANNER_HPP

#include "simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class ShadowSequencePlanner {
public:
    static SimPlan planSequence(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                                bool down, int limit, float deltaTime, float firstDt,
                                PlanOptions const& options);

private:
    ShadowSequencePlanner() = delete;
};

#endif
