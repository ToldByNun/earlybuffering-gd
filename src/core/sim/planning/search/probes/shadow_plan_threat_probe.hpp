#ifndef SHADOW_PLAN_THREAT_PROBE_HPP
#define SHADOW_PLAN_THREAT_PROBE_HPP

#include "../shadow_plan_context.hpp"

class ShadowPlanContext;

class ShadowPlanThreatProbe {
public:
    static bool evaluate(ShadowPlanContext& context);

private:
    ShadowPlanThreatProbe() = delete;
};

#endif
