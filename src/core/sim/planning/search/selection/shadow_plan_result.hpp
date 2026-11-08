#ifndef SHADOW_PLAN_RESULT_HPP
#define SHADOW_PLAN_RESULT_HPP

#include "../shadow_plan_context.hpp"

class ShadowPlanContext;

class ShadowPlanResult {
public:
    static void publish(ShadowPlanContext& context);

private:
    ShadowPlanResult() = delete;
};

#endif
