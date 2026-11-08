#ifndef SHADOW_PLAN_INPUT_WINDOW_HPP
#define SHADOW_PLAN_INPUT_WINDOW_HPP

#include "../shadow_plan_context.hpp"

class ShadowPlanContext;

class ShadowPlanInputWindow {
public:
    static bool evaluate(ShadowPlanContext& context);

private:
    ShadowPlanInputWindow() = delete;
};

#endif
