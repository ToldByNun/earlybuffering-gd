#ifndef SHADOW_PLAN_SEARCH_HPP
#define SHADOW_PLAN_SEARCH_HPP

#include "../simulation_plan.hpp"
#include "shadow_plan_context.hpp"

#include "../../shadow_simulator.hpp"

class EbShadowPlanContext;

class EbShadowPlanSearch {
public:
    static EbSimPlan run(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                       int limit, float deltaTime, float firstDt, EbPlanOptions const& options);

private:
    static void prepare(EbShadowPlanContext& context);
    EbShadowPlanSearch() = delete;
};

#endif
