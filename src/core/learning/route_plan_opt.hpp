#ifndef ROUTE_PLAN_OPT_HPP
#define ROUTE_PLAN_OPT_HPP

#include <cstdint>
#include <string>
#include <vector>

// Assist Pack AP-NL8 — planner hot-path opts (pooling, subsample, window cache,
// hi≤lo early-out, seed/expand/verify µs). Budget abort stays PartiallyChecked.
class EbRoutePlanOpt {
public:
    class WindowBounds {
    public:
        int lo = 0;
        int hi = 0;
        int limit = 0;
        int candidateWindow = 0;
        int maxDelay = 0;
        int minDelay = 0;
        bool valid = false;

        void clear() {
            lo = hi = limit = candidateWindow = maxDelay = minDelay = 0;
            valid = false;
        }

    private:
    };

    class Timing {
    public:
        int seedUs = 0;
        int expandUs = 0;
        int verifyUs = 0;
        int totalUs = 0;
        bool budgetHit = false;

        void clear() {
            seedUs = expandUs = verifyUs = totalUs = 0;
            budgetHit = false;
        }

    private:
    };

    // Cached lo/hi from cfg + limit. hi < lo ⇒ empty window (early-out).
    static WindowBounds computeWindow(int minDelay, int maxDelay, int candidateWindow, int limit);
    static bool windowEmpty(WindowBounds const& w);
    static bool windowCacheHit(WindowBounds const& cached, int minDelay, int maxDelay,
                               int candidateWindow, int limit);

    // Stride-subsample delays to ≤ budget, always keeping front + back when non-empty.
    static std::vector<int> subsampleDelays(std::vector<int> const& delays, int budget);

    // Nova-style stride over [lo,hi]: max((hi-lo)/60, 1); empty when hi < lo.
    static int subsampleStep(int lo, int hi);
    static std::vector<int> linearSubsample(int lo, int hi);

    // Scratch reuse helpers (caller owns vectors; clear+reserve without free).
    static void reuseIntScratch(std::vector<int>& scratch, size_t reserveHint);
    template <class T>
    static void reuseScratch(std::vector<T>& scratch, size_t reserveHint) {
        scratch.clear();
        if (scratch.capacity() < reserveHint)
            scratch.reserve(reserveHint);
    }

    // Overlay fragment: "seed=12µs exp=40µs ver=80µs tot=140µs".
    static std::string timingOverlayBit(Timing const& t);

    // Budget honesty: truncated/budget ⇒ never claim ShadowVerified.
    static bool budgetForbidsShadowVerified(bool budgetHit, bool truncated, bool unknown);

    static bool selfCheck(std::string& report);

private:
    EbRoutePlanOpt() = delete;
};

#endif
