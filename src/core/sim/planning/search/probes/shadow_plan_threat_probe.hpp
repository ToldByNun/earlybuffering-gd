#ifndef SHADOW_PLAN_THREAT_PROBE_HPP
#define SHADOW_PLAN_THREAT_PROBE_HPP

#include "../shadow_plan_context.hpp"

class EbShadowPlanContext;

class EbShadowPlanThreatProbe {
public:
    static bool evaluate(EbShadowPlanContext& context);

private:
    EbShadowPlanThreatProbe() = delete;
};

#endif
