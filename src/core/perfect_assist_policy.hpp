#ifndef PERFECT_ASSIST_POLICY_HPP
#define PERFECT_ASSIST_POLICY_HPP

#include "assist_verdict.hpp"
#include <string>

// Commit 11 — PerfectAssistPolicy (ideas reimplemented; no upstream).
// Opportunity + Trust → MUST Delay-search. Named Pass for Untrust / Drift /
// HardUnknown / TwEmpty / budget-no-survivor. Never soft-trust budgetHit Delay;
// never invent auto-click / auto-release. InterveneGate = help-don't-hurt.
class PerfectAssistPolicy {
public:
    class OpportunityInput {
    public:
        bool foresightOrbPad = false;
        bool threatZoneValid = false;
        bool idleSafer = false;
        bool ascentUseful = false;
        bool bakePreferBand = false;
        bool orbWindowFound = false;
        bool d0ActivatedRing = false;
        bool probeShowsGain = false; // OpportunityProbe mid/limit gain

    private:
    };

    class TrustInput {
    public:
        bool parityDrift = false;
        bool spotUntrusted = false;
        bool divergenceUntrusted = false;
        bool twEmptyWhenExpected = false;
        bool hardUnknownHit = false;

    private:
    };

    enum class Decision { Pass = 0, MustDelaySearch, AllowVanillaNoThreat };

    static bool isOpportunity(OpportunityInput const& o);
    static bool trustOk(TrustInput const& t);

    // Named Pass when trust fails (priority: parity > spot > tw > hard-unk > untrust).
    static bool forcePassReason(TrustInput const& t, AssistReason& out);

    static Decision classify(TrustInput const& t, OpportunityInput const& o);

    // Never PassBudget while trust+opportunity and budget can still probe a survivor.
    static bool allowPassBudget(bool trustOkFlag, bool opportunity, bool budgetRemainsForProbe);

    // Soft-trust ban: never invent Delay from incomplete budget-partial search.
    // A fully evaluated viable cand may arm even if later delays hit the budget.
    static bool allowArmDelay(bool candViable, bool budgetHit, bool softTrustPartial = false);

    // Never invent next-click / auto-release.
    static bool allowInventClick();
    static bool allowInventRelease();

    // InterveneGate (help-don't-hurt): Delay only if clearer than d0 / improves.
    static bool interveneOk(bool d0Viable, bool delayViable, bool delayClearsBetter);

    // Wave: among two clears, prefer smallest clear that still has +1px margin.
    static bool preferSmallerWaveClear(float clearA, float clearB, float marginPx = 1.f);

    static bool selfCheck(std::string& report);

private:
    PerfectAssistPolicy() = delete;
};

#endif
