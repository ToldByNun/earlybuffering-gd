#ifndef SHADOW_PLAN_RESULT_HPP
#define SHADOW_PLAN_RESULT_HPP

#include "../shadow_plan_context.hpp"

class EbShadowPlanContext;

class EbShadowPlanResult {
public:
    static void publish(EbShadowPlanContext& context);

private:
    EbShadowPlanResult() = delete;
};

#endif
