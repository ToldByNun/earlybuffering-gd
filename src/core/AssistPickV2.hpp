#ifndef EARLYBUFFER_CORE_ASSISTPICKV2_HPP
#define EARLYBUFFER_CORE_ASSISTPICKV2_HPP

#include <string>
#include <vector>

// Live choose for assist-pick **v2**.
// Foresight-first: among ±k-robust viable delays, rank by
//   orbUseful > surviveBin > surviveTicks > endX − λ·delay
// (not v1's latest-robust). Orb band still earliest activation.
// Pure over cand views — no PlayLayer / sim. ShadowSimulator owns jitter.
class AssistPickV2 {
public:
    struct CandView {
        bool viable = false;
        bool activatedRing = false;
        bool orbUseful = false;
        int surviveBin = 0;
        int surviveTicks = 0;
        float endX = 0.f;
    };

    struct Context {
        bool idleSafer = false;
        bool ascentUseful = false;
        bool longForesightNeedSearch = false;
        float latencyWeight = 0.f;
    };

    struct Result {
        int chosen = -1;
        bool doPass = false;
        bool requireOrb = false;
    };

    // Lexico foresight compare: true if a (at delayA) beats b (at delayB).
    static bool foresightBetter(
        CandView const& a,
        int delayA,
        CandView const& b,
        int delayB,
        float latencyWeight
    );

    static int pickEarliestRobust(
        CandView const* cands,
        int limit,
        int robustK,
        std::vector<int> const& pool,
        bool needActivation
    );

    static int pickLatestRobust(
        CandView const* cands,
        int limit,
        int robustK,
        std::vector<int> const& pool,
        bool needActivation
    );

    // Best ±k-robust (else any viable) in pool by foresightBetter.
    static int pickBestForesight(
        CandView const* cands,
        int limit,
        int robustK,
        std::vector<int> const& pool,
        bool needActivation,
        float latencyWeight
    );

    // Orb → earliest ±k-robust in band.
    // Else foresight-rank; idleSafer / longForesightNeedSearch prefer Delay
    // when a later cand beats or matches d0 foresight.
    static Result choose(
        CandView const* cands,
        int limit,
        int robustK,
        bool orbIntent,
        int actMin,
        int actMax,
        Context const& ctx = {}
    );

    static bool selfCheck(std::string& report);

private:
    AssistPickV2() = default;

    static bool neighborRobust(
        CandView const* cands,
        int limit,
        int robustK,
        int delay,
        bool needActivation
    );
};

#endif // EARLYBUFFER_CORE_ASSISTPICKV2_HPP
