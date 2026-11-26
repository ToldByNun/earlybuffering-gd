#ifndef ROUTE_EXPAND_CORE_HPP
#define ROUTE_EXPAND_CORE_HPP

#include "route_cand_semantics.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Assist Pack AP-NL4 — clean-room shared expand + sim-core.
// Press/release growth around a seed, QPC/budget honesty, occupancy ring,
// stuck/memory probe without neural. Survival comes only from injected sim
// outcomes (verifyDelay / runOne in live callers) — never from neural score.
class EbRouteExpandCore {
public:
    // Claimed absolute ticks so two routes cannot double-arm the same tick.
    class OccupancyRing {
    public:
        static constexpr int kBits = 1024;

        bool tryClaim(int absTick);
        void release(int absTick);
        bool claimed(int absTick) const;
        void clear();
        int claimedCount() const {
            return m_count;
        }

    private:
        bool m_bits[kBits]{};
        int m_count = 0;

        static int slot(int absTick);
    };

    class Options {
    public:
        int budgetUs = 4000;
        int horizonTicks = 64;
        int maxEvents = 8;
        int maxSimCalls = 24;
        bool generateRelease = true;
        std::vector<int> holdChoices = {4, 8, 12};

    private:
    };

    // Injected sim classification (live: verifyDelay/runOne; tests: synthetic).
    class SimOutcome {
    public:
        bool ok = false;
        bool unknown = false;
        bool budgetHit = false;
        float progress = 0.f;
        bool clearsThreat = false;

    private:
    };

    using SimProbe = std::function<SimOutcome(EbAiRouteEvent::Kind kind, int delayTicks,
                                              int holdTicks, int simIndex)>;

    class Result {
    public:
        EbRouteCandSemantics::ExpandJob job;
        int simCalls = 0;
        bool truncated = false;    // budget / maxSim / horizon cut short
        bool stuckProbeOnly = false;
        char const* reason = "empty";

    private:
    };

    // Expand press seed → optional release candidates. Occupancy claims planTick+delay.
    static Result expandAroundSeed(std::uint32_t jobId, EbRouteCandSemantics::Cand seed,
                                   int planTick, Options const& opts, OccupancyRing& ring,
                                   SimProbe const& probe);

    // Stuck/memory fast path: sim-core only (no neural ranking). Single press probe.
    static Result stuckSimCoreProbe(std::uint32_t jobId, int planTick, int delayTicks,
                                    Options const& opts, OccupancyRing& ring,
                                    SimProbe const& probe);

    // Honesty: truncated / unknown / budgetHit ⇒ not ShadowVerified and not armable.
    static bool honestyOk(Result const& result);

    static bool selfCheck(std::string& report);

private:
    EbRouteExpandCore() = delete;

    static bool budgetExhausted(Options const& opts, int elapsedUsApprox, bool outcomeBudgetHit);
};
#endif
