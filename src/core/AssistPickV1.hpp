#ifndef EARLYBUFFER_CORE_ASSISTPICKV1_HPP
#define EARLYBUFFER_CORE_ASSISTPICKV1_HPP

#include <string>
#include <vector>

// Frozen Live choose for assist-pick **v1** (former Legacy).
// Pure over cand viability / ring activation — no PlayLayer / sim.
// ShadowSimulator owns jitter + New/v2 ranking; this class is the v1 policy.
class AssistPickV1 {
public:
    struct CandView {
        bool viable = false;
        bool activatedRing = false;
    };

    struct Result {
        int chosen = -1;     // delay ticks; 0 + doPass = Pass-now
        bool doPass = false;
        bool requireOrb = false;
    };

    // Earliest delay in pool that is ±k-robust viable (optional ring activation).
    // Fallback: first viable in pool order. -1 if none.
    static int pickEarliestRobust(
        CandView const* cands,
        int limit,
        int robustK,
        std::vector<int> const& pool,
        bool needActivation
    );

    // Latest delay in pool that is ±k-robust viable (optional ring activation).
    // Fallback: last viable in pool order. -1 if none.
    static int pickLatestRobust(
        CandView const* cands,
        int limit,
        int robustK,
        std::vector<int> const& pool,
        bool needActivation
    );

    // Full v1 choose (no jitter):
    //  - orbIntent + activation band → earliest ±k-robust in band (requireOrb)
    //  - else d0 viable → Pass (chosen=0)
    //  - else latest ±k-robust in 0..limit
    static Result choose(
        CandView const* cands,
        int limit,
        int robustK,
        bool orbIntent,
        int actMin,
        int actMax
    );

    static bool selfCheck(std::string& report);

private:
    AssistPickV1() = default;

    static bool neighborRobust(
        CandView const* cands,
        int limit,
        int robustK,
        int delay,
        bool needActivation
    );
};

#endif
