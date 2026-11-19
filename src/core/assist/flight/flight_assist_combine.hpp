#ifndef FLIGHT_ASSIST_COMBINE_HPP
#define FLIGHT_ASSIST_COMBINE_HPP

#include <cstdint>
#include <span>
#include <string>

// AP09: one decision order for flight intent, Wave alternation, and ordinary assist.
// Binding priority (high → low): direction change → collision rescue → ship line →
// wave rhythm → ordinary pass-through. Planners propose; this policy selects the lane.
class EbFlightAssistCombine {
public:
    enum class Lane {
        DirectionChange = 0,
        CollisionRescue = 1,
        FlightLine = 2,
        AlternateRhythm = 3,
        Ordinary = 4,
    };

    class EdgeContext {
    public:
        bool lifecycleOk = true;
        bool directionChange = false;
        bool baselineDied = false;
        bool shipLineArmed = false;
        bool wavePatternArmed = false;
        bool shipTracking = false;
        bool waveTracking = false;
        bool dashHold = false;
        bool orbTiming = false;
        double waveConfidence = 0.;
        double shipConfidence = 0.;

    private:
    };

    // Candidate-cache identity for a single plan attempt. Reuse only when equal.
    class CacheKey {
    public:
        std::uint64_t worldStamp = 0;
        std::uint64_t playerStamp = 0;
        int effectTick = -1;
        int edgeSequence = -1;
        int phaseMillis = -1;
        int settingsStamp = 0;
        float timeWarp = 1.f;
        bool portalEpoch = false;

        static bool reuseAllowed(CacheKey const& stored, CacheKey const& live);
        static CacheKey invalidate();

    private:
    };

    static Lane selectLane(EdgeContext const& context);
    static char const* laneName(Lane lane);

    // Sustained climb/dive / motif yield cancels pending flight timing.
    static bool shouldCancelPending(bool yieldRequested, bool pendingOwned);

    // Learning/reset must not leave a flight-owned buffer on old evidence.
    static bool resetClearsPending(bool learnedReset, bool flightOwnedPending);

    // Motif confidence ranks Alternate above Ordinary only while Tracking.
    static bool preferAlternateOverOrdinary(bool waveTracking, double confidence);

    // Selected-tick guard: never move a verified flight delay onto an unverified
    // learned tick. Incomplete probe → keep planned (or drop to 0 when budget-hit).
    static int guardFlightDelay(int proposed, int planned, std::span<int const> verified,
                                bool budgetIncomplete);

    // Identical inputs + equal budgets → identical lane (determinism contract).
    static Lane selectLaneDeterministic(EdgeContext const& context, int budgetRemainingUs);

    static bool selfCheck(std::string& report);

private:
    EbFlightAssistCombine() = delete;
};

#endif
