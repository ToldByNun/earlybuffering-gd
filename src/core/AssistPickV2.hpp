#ifndef EARLYBUFFER_CORE_ASSISTPICKV2_HPP
#define EARLYBUFFER_CORE_ASSISTPICKV2_HPP

#include <string>
#include <vector>

// Live choose for assist-pick **v2**.
// Skeleton (WU10): same policy as AssistPickV1 via thin delegate.
// WU11 fills long-path / foresight ranking using surviveBin / endX / Context.
// Pure over cand views — no PlayLayer / sim. ShadowSimulator owns jitter.
class AssistPickV2 {
public:
    struct CandView {
        bool viable = false;
        bool activatedRing = false;
        // Reserved for WU11 ranking (ignored by skeleton choose).
        int surviveBin = 0;
        float endX = 0.f;
    };

    // Optional foresight / miss-assist hints (ignored by skeleton choose).
    struct Context {
        bool idleSafer = false;
        bool ascentUseful = false;
        bool longForesightNeedSearch = false;
    };

    struct Result {
        int chosen = -1;
        bool doPass = false;
        bool requireOrb = false;
    };

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

    // Skeleton: same rules as AssistPickV1::choose (Context unused).
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
};

#endif // EARLYBUFFER_CORE_ASSISTPICKV2_HPP
