#ifndef AI_ROUTE_PLANNER_HPP
#define AI_ROUTE_PLANNER_HPP

#include "ai_config.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include <cstdint>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// Multi-event press/release route for AI Mode (Nova-style ring search, EB Shadow verify).
class EbAiRouteEvent {
public:
    enum class Kind : std::uint8_t { Press = 0, Release = 1 };

    Kind kind = Kind::Press;
    int delay = 0;     // ticks from route start (relative to plan tick)
    int holdTicks = 0; // for Press: planned hold before Release
    float neuralScore = 0.f;
    bool verified = false;
    bool clearsThreat = false;

private:
};

class EbAiRoute {
public:
    std::vector<EbAiRouteEvent> events;
    int planTick = -1;
    float startX = 0.f;
    float routeScore = 0.f;
    int simCalls = 0;
    int neuralEvals = 0;
    bool budgetHit = false;
    bool valid = false;
    char const* reason = "empty";

    void clear() {
        events.clear();
        planTick = -1;
        startX = 0.f;
        routeScore = 0.f;
        simCalls = 0;
        neuralEvals = 0;
        budgetHit = false;
        valid = false;
        reason = "empty";
    }

private:
};

class EbAiRoutePlanner {
public:
    static EbAiRoutePlanner& get();

    // Build a verified multi-event route from the current live state.
    EbAiRoute plan(PlayLayer* layer, PlayerObject* player, bool player1, int tick, int limit,
                   float dt);

    EbAiRoute const& lastRoute() const {
        return m_last;
    }
    void invalidate(char const* why);
    bool needsReplan(int tick, float liveX) const;

    // Diagnostics counters (reset on open/level).
    int plans = 0;
    int invalidations = 0;
    int budgetHits = 0;
    int verifyHits = 0;
    int verifyTries = 0;

    // Convert route events → absolute-tick press/release timeline for Shadow replay.
    static std::vector<EbInputEvent> timelineFromEvents(std::vector<EbAiRouteEvent> const& events,
                                                        int planTick);
    // Passes gating: requires confidenceThreshold AND minCandidateConfidence when enabled.
    static bool passesGate(float neural, EbAiConfig const& cfg);
    // Keep the best adaptivePercentile fraction of a descending-sorted score list.
    static void keepTopPercentile(std::vector<float>& descendingScores, float percentile,
                                  float& outFloor);

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;
    EbAiRoutePlanner() = default;

    class EbAiScoredCandidate {
    public:
        int delay = 0;
        bool press = true;
        float neural = 0.f;

    private:
    };

    class EbAiBeamNode {
    public:
        std::vector<EbAiRouteEvent> events;
        float score = 0.f;
        bool holding = false; // true after verified Press with no Release yet
        int lastAbsDelay = 0; // last event delay relative to plan start
        bool ok = false;

    private:
    };

    class EbAiVerifyOutcome {
    public:
        bool ok = false;
        bool clearsThreat = false;
        bool budgetHit = false;
        bool unknown = false;
        float progress = 0.f;

    private:
    };

    EbAiRoute m_last;
    int m_lastPlanTick = -1;
    float m_lastPlanX = 0.f;

    std::vector<int> enumerateDelays(EbAiConfig const& cfg, int limit) const;
    float encodeAndScore(float const* baseFeatures, int delay, bool press) const;
    // Verify the FULL event sequence from a start snapshot (chained correctness).
    EbAiVerifyOutcome verifyTimeline(PlayLayer* layer, int snapshotSlot, int planTick,
                                     std::vector<EbAiRouteEvent> const& events, float dt,
                                     EbAiConfig const& cfg);
    // Single-action verify (depth-1 seed / orb path). Prefer verifyTimeline for depth>1.
    bool verifyCandidate(PlayLayer* layer, PlayerObject* player, bool press, int delay, int limit,
                         float dt, EbAiConfig const& cfg, float& outProgress,
                         bool& outClearsThreat);
};

#endif
