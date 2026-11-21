#ifndef AI_ROUTE_PLANNER_HPP
#define AI_ROUTE_PLANNER_HPP

#include "ai_config.hpp"
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
    int delay = 0;          // ticks from route start (absolute relative to plan tick)
    int holdTicks = 0;      // for Press: planned hold before Release
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

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;
    EbAiRoutePlanner() = default;

    class EbAiScoredCandidate {
    public:
        int delay = 0;
        bool press = true;
        float neural = 0.f;
        float routePartial = 0.f;

    private:
    };

    EbAiRoute m_last;
    int m_lastPlanTick = -1;
    float m_lastPlanX = 0.f;

    std::vector<int> enumerateDelays(EbAiConfig const& cfg, int limit) const;
    float encodeAndScore(float const* baseFeatures, int delay, bool press) const;
    bool verifyCandidate(PlayLayer* layer, PlayerObject* player, bool press, int delay, int limit,
                         float dt, EbAiConfig const& cfg, float& outProgress,
                         bool& outClearsThreat);
};

#endif
