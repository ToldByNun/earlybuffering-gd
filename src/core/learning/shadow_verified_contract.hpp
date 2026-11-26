#ifndef SHADOW_VERIFIED_CONTRACT_HPP
#define SHADOW_VERIFIED_CONTRACT_HPP

#include "ai_route.hpp"
#include "route_cand_semantics.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Assist Pack AP-NL5b — strict full-route ShadowVerified (DR3).
// A route is ShadowVerified only when every planned event was simulated,
// horizon covers last event + settle, and result is not unknown/budget/truncated.
// Autoplay arms only from ShadowVerified (or Explore explicitly labeled unverified).
class EbShadowVerifiedContract {
public:
    static constexpr int kDefaultSettleTicks = 1;

    // Read-only DR3 checks (do not mutate job).
    static bool allEventsSimulated(EbRouteCandSemantics::ExpandJob const& job);
    static bool horizonCovers(EbRouteCandSemantics::ExpandJob const& job, int settleTicks);
    static bool rejectsUnknownOrBudget(EbRouteCandSemantics::ExpandJob const& job);

    // Full ShadowVerified predicate (settle-aware). Job state may already be set;
    // this re-evaluates honesty independently of labels.
    static bool isShadowVerified(EbRouteCandSemantics::ExpandJob const& job,
                                 int settleTicks = kDefaultSettleTicks);

    // Classify from event flags without trusting a prior LifeState label.
    // Tail never simulated → PartiallyChecked (never ShadowVerified).
    static EbRouteCandSemantics::LifeState classify(
        EbRouteCandSemantics::ExpandJob const& job, int settleTicks = kDefaultSettleTicks);

    // Build/finalize helper: apply settleTicks then finalizeExpand honesty.
    static EbRouteCandSemantics::LifeState finalizeStrict(EbRouteCandSemantics::ExpandJob& job,
                                                          int settleTicks = kDefaultSettleTicks);

    // Execution binder: scheduled sequence must equal verified (order + relative delays).
    static bool sequencesMatch(std::vector<EbAiRouteEvent> const& verified,
                               std::vector<EbAiRouteEvent> const& scheduled);

    // Autoplay arm gate. Explore must be labeled unverified separately.
    static bool mayArmAutoplay(EbRouteCandSemantics::ExpandJob const& job,
                               bool exploreLabeledUnverified);

    // Route-level: every event.verified, contiguous, no budget/unknown.
    static bool routeFullyVerified(std::vector<EbAiRouteEvent> const& events, bool budgetHit,
                                   bool unknown);

    static bool selfCheck(std::string& report);

private:
    EbShadowVerifiedContract() = delete;
};

#endif
