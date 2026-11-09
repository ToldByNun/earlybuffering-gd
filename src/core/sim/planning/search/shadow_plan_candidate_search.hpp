#ifndef SHADOW_PLAN_CANDIDATE_SEARCH_HPP
#define SHADOW_PLAN_CANDIDATE_SEARCH_HPP

#include "shadow_plan_context.hpp"

class EbShadowPlanContext;

class EbShadowPlanCandidateSearch {
public:
    static void evaluate(EbShadowPlanContext& context);

private:
    EbShadowPlanCandidateSearch() = delete;
};

#endif
