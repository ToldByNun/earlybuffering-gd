#ifndef SHADOW_PLAN_SEARCH_HPP
#define SHADOW_PLAN_SEARCH_HPP

#include "../simulation_plan.hpp"
#include "shadow_plan_context.hpp"

#include "../../shadow_simulator.hpp"

class ShadowPlanContext;

class ShadowPlanSearch {
public:
    static SimPlan run(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                       int limit, float deltaTime, float firstDt, PlanOptions const& options);

private:
    static void prepare(ShadowPlanContext& context);
    ShadowPlanSearch() = delete;
};

#endif
