#ifndef SHADOW_SEQUENCE_PLANNER_HPP
#define SHADOW_SEQUENCE_PLANNER_HPP

#include "simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class EbShadowSequencePlanner {
public:
    static EbSimPlan planSequence(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                                bool down, int limit, float deltaTime, float firstDt,
                                EbPlanOptions const& options);

private:
    EbShadowSequencePlanner() = delete;
};

#endif
