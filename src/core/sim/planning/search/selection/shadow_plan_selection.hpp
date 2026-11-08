#ifndef SHADOW_PLAN_SELECTION_HPP
#define SHADOW_PLAN_SELECTION_HPP

#include "../shadow_plan_context.hpp"

class ShadowPlanContext;

class ShadowPlanSelection {
public:
    static bool choose(ShadowPlanContext& context);

private:
    ShadowPlanSelection() = delete;
};

#endif
