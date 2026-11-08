#ifndef SHADOW_PLAN_CANDIDATE_SEARCH_HPP
#define SHADOW_PLAN_CANDIDATE_SEARCH_HPP

#include "shadow_plan_context.hpp"

class ShadowPlanContext;

class ShadowPlanCandidateSearch {
public:
    static void evaluate(ShadowPlanContext& context);

private:
    ShadowPlanCandidateSearch() = delete;
};

#endif
