#ifndef ASSIST_PICK_V2_HPP
#define ASSIST_PICK_V2_HPP

#include <string>
#include <vector>

// Live choose for assist-pick **v2**.
// Foresight-first: among ±k-robust Delay-worthy delays, rank by
//   orbUseful > surviveBin > surviveTicks > endX − λ·delay
// (not v1's latest-robust). Orb band still earliest activation.
// Delay-worthy = viable && !died (Alive escape). Force-search never
// invents Delay when every later cand is dead.
// Pure over cand views — no PlayLayer / sim. ShadowSimulator owns jitter.
class EbAssistPickV2 {
public:
    class CandView {
    public:
        bool viable = false;
        bool activatedRing = false;
        bool orbUseful = false;
        int surviveBin = 0;
        int surviveTicks = 0;
        float endX = 0.f;
        // Ghost died on this delay (soft Landing/LateCrash may still be
        // "viable" only on bake paths — Live Delay requires !died).
        bool died = false;

    private:
    };

    class Context {
    public:
        bool idleSafer = false;
        bool ascentUseful = false;
        bool longForesightNeedSearch = false;
        float latencyWeight = 0.f;

    private:
    };

    class Result {
    public:
        int chosen = -1;
        bool doPass = false;
        bool requireOrb = false;

    private:
    };

    // Live Delay-arm / force-search: still-living viable ghost only.
    static bool isDelayWorthy(CandView const& c);

    // Lexico foresight compare: true if a (at delayA) beats b (at delayB).
    static bool foresightBetter(CandView const& a, int delayA, CandView const& b, int delayB,
                                float latencyWeight);

    static int pickEarliestRobust(CandView const* cands, int limit, int robustK,
                                  std::vector<int> const& pool, bool needActivation);

    static int pickLatestRobust(CandView const* cands, int limit, int robustK,
                                std::vector<int> const& pool, bool needActivation);

    // Best ±k-robust (else any viable) in pool by foresightBetter.
    static int pickBestForesight(CandView const* cands, int limit, int robustK,
                                 std::vector<int> const& pool, bool needActivation,
                                 float latencyWeight);

    // Orb → earliest ±k-robust Delay-worthy in band.
    // Else foresight-rank; idleSafer / LF / ascent prefer a later Alive Delay
    // when one exists — otherwise hopeless (chosen=-1), never soft-dead Delay.
    // Overload without Context — Clang rejects default args that need nested
    // default member initializers (`ctx = {}`) inside the enclosing class.
    static Result choose(CandView const* cands, int limit, int robustK, bool orbIntent, int actMin,
                         int actMax);
    static Result choose(CandView const* cands, int limit, int robustK, bool orbIntent, int actMin,
                         int actMax, Context const& ctx);

    static bool selfCheck(std::string& report);

private:
    EbAssistPickV2() = default;

    static bool neighborRobust(CandView const* cands, int limit, int robustK, int delay,
                               bool needActivation);
};

#endif
