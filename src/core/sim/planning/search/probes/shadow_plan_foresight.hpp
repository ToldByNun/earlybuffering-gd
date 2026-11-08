#ifndef SHADOW_PLAN_FORESIGHT_HPP
#define SHADOW_PLAN_FORESIGHT_HPP

#include "../shadow_plan_context.hpp"

class ShadowPlanContext;

class ShadowPlanForesight {
public:
    static void probe(ShadowPlanContext& context);

private:
    ShadowPlanForesight() = delete;
};

#endif
