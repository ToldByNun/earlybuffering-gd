#ifndef SHADOW_PLAN_SELECTION_HPP
#define SHADOW_PLAN_SELECTION_HPP

#include "../shadow_plan_context.hpp"

class EbShadowPlanContext;

class EbShadowPlanSelection {
public:
    static bool choose(EbShadowPlanContext& context);

private:
    EbShadowPlanSelection() = delete;
};

#endif
