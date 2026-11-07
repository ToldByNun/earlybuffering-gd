#ifndef OPPORTUNITY_PROBE_HPP
#define OPPORTUNITY_PROBE_HPP

#include <string>

// G2 gate before Pass no-threat: foresight or cheap mid/limit probes may
// force a full delay search even when d0 survives with no threat zone.
class OpportunityProbe {
public:
    // Same endX gain idea as wave futile (ShadowSimulator::kFutileEndXGain).
    static constexpr float kEndXGain = 8.f;
    // Min ticks left in budget to attempt one probe runOne-worth of work.
    static constexpr int kMinBudgetForProbe = 24;
    // Cap press-to-reach orb scan extras.
    static constexpr int kMaxPressReachExtraTicks = 48;

    struct Sample {
        bool activatedRing = false;
        bool diedBeforeAction = false;
        int surviveBin = 0;
        float endX = 0.f;
    };

    // Delay for the mid probe (limit/2), clamped to [1, limit].
    static int midDelay(int limit);

    // Orb-window coast scan length: max(limit+slack+4, limit+1).
    static int orbScanTicks(int limit, int slack);

    // Extra press-held scan ticks when coast scan missed (capped).
    static int pressReachScanTicks(int limit, int slack);

    // Probe improves on baseline (activation miss, better bin, or endX gain).
    static bool sampleShowsOpportunity(Sample const& baseline, Sample const& probe);

    // True → do NOT Pass no-threat; continue full cand search.
    // foresightOrbPad / orbWindowFound / d0Activated already force search.
    static bool shouldSearchFurther(
        bool foresightOrbPad,
        bool orbWindowFound,
        bool d0ActivatedRing,
        Sample const& baseline,
        Sample const* probes,
        int probeCount
    );

    // Allow PassNoThreat only when search-further is false.
    static bool allowPassNoThreat(
        bool foresightOrbPad,
        bool orbWindowFound,
        bool d0ActivatedRing,
        Sample const& baseline,
        Sample const* probes,
        int probeCount
    );

    static bool selfCheck(std::string& report);

private:
    OpportunityProbe() = default;
};

#endif // OPPORTUNITY_PROBE_HPP
