#ifndef SHADOW_PLAN_INPUT_WINDOW_HPP
#define SHADOW_PLAN_INPUT_WINDOW_HPP

#include "../shadow_plan_context.hpp"

class EbShadowPlanContext;

class EbShadowPlanInputWindow {
public:
    static bool evaluate(EbShadowPlanContext& context);

private:
    EbShadowPlanInputWindow() = delete;
};

#endif
