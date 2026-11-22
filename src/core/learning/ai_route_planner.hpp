#ifndef AI_ROUTE_PLANNER_HPP
#define AI_ROUTE_PLANNER_HPP

#include "ai_config.hpp"
#include "ai_route.hpp"
#include "neural_seed_strategy.hpp"
#include "route_cand_semantics.hpp"
#include "route_expand_core.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include <cstdint>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// Multi-event press/release route planner for AI Mode (Nova-style search, EB Shadow verify).
class EbAiRoutePlanner {
public:
    static EbAiRoutePlanner& get();

    // Build a verified multi-event route from the current live state.
    EbAiRoute plan(PlayLayer* layer, PlayerObject* player, bool player1, int tick, int limit,
                   float dt);

    EbAiRoute const& lastRoute() const {
        return m_last;
    }
    // AP-NL3: last expand job / seed cands (Autoplay write; Live read-only diagnose).
    EbRouteCandSemantics::ExpandJob const& lastExpandJob() const {
        return m_lastExpandJob;
    }
    std::vector<EbRouteCandSemantics::Cand> const& lastSeedCands() const {
        return m_lastSeedCands;
    }
    std::string diagnoseLastExpand() const;
    // AP-NL4: shared expand around a seed (verifyDelay probe) + occupancy.
    EbRouteExpandCore::Result expandSeedShared(PlayLayer* layer, PlayerObject* player,
                                               EbRouteCandSemantics::Cand const& seed, int tick,
                                               int limit, float dt);
    // Stuck/memory sim-core probe — no neural ranking.
    EbRouteExpandCore::Result stuckSimCore(PlayLayer* layer, PlayerObject* player, int tick,
                                           int delayTicks, int limit, float dt);
    void invalidate(char const* why);
    bool needsReplan(int tick, float liveX) const;

    // Diagnostics counters (reset on open/level).
    int plans = 0;
    int invalidations = 0;
    int budgetHits = 0;
    int verifyHits = 0;
    int verifyTries = 0;
    // AP-NL5: dual-seed diagnostics (never conflate with survival).
    EbNeuralSeedStrategy::Counters seedCounters;

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
    EbRouteCandSemantics::ExpandJob m_lastExpandJob;
    std::vector<EbRouteCandSemantics::Cand> m_lastSeedCands;
    EbRouteExpandCore::OccupancyRing m_occupancy;
    EbRouteExpandCore::Result m_lastExpandResult;
    EbNeuralSeedStrategy::Kind m_lastSeedKind = EbNeuralSeedStrategy::Kind::Enhanced;
    std::uint32_t m_nextCandId = 1;
    std::uint32_t m_nextJobId = 1;
    int m_lastPlanTick = -1;
    float m_lastPlanX = 0.f;

    std::vector<int> enumerateDelays(EbAiConfig const& cfg, int limit) const;
    float encodeAndScore(float const* baseFeatures, int delay, bool press) const;
    // Verify the FULL event sequence from a start snapshot (chained correctness).
    EbAiVerifyOutcome verifyTimeline(PlayLayer* layer, int snapshotSlot, int planTick,
                                     std::vector<EbAiRouteEvent> const& events, float dt,
                                     EbAiConfig const& cfg, int budgetUs = 0);
    // Single-action verify (depth-1 seed / orb path). Prefer verifyTimeline for depth>1.
    bool verifyCandidate(PlayLayer* layer, PlayerObject* player, bool press, int delay, int limit,
                         float dt, EbAiConfig const& cfg, float& outProgress,
                         bool& outClearsThreat);
};

#endif
