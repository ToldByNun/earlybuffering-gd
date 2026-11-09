#ifndef SHADOW_PLAN_FORESIGHT_HPP
#define SHADOW_PLAN_FORESIGHT_HPP

#include "../shadow_plan_context.hpp"

class EbShadowPlanContext;

class EbShadowPlanForesight {
public:
    static void probe(EbShadowPlanContext& context);

private:
    EbShadowPlanForesight() = delete;
};

#endif
