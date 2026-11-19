#ifndef HYBRID_LEARNING_HPP
#define HYBRID_LEARNING_HPP
#include "hybrid_model.hpp"
#include "hybrid_observation.hpp"
#include <array>
#include <vector>
class PlayLayer;
class PlayerObject;
class EbPlanOptions;
class EbSimPlan;

class EbHybridLearning {
public:
    static EbHybridLearning& get();
    static bool enabled();
    void open(std::filesystem::path path);
    void close();
    void reset();
    void save();
    void settingsChanged();
    void boundary(bool newAttempt);
    void edge(bool player1, int tick, bool down);
    int prepare(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int tick,
                int limit, float dt, float firstDt, EbPlanOptions const& options);
    // After prepare(): UCB-ranked delays (best / under-explored first) then interpolated mids.
    std::vector<int> rankedVerifyDelays(int limit) const;
    // Immediate verify feedback from hybrid-first probes (player-driven physics RL).
    void noteVerifyOutcome(int delay, bool verifiedSafe);
    void vanilla(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int tick,
                 float dt);
    void decision(bool player1, EbSimPlan const& plan);
    void fired(bool player1, int planId, int delay, float phase);
    void poll(PlayLayer* layer, int tick);
    void death(bool player1, int tick);
    int familiar() const;
    int uniqueKeys() const;
    unsigned totalPulls() const;
    std::vector<EbHybridModel::Inspection> inspect() const;
    void freeze(bool value);
    bool frozen() const;
    void applyCapacityLimits();
    int interrupted = 0;
    int deaths = 0;
    int lastDeathAge = -1;
    int lastDelay = 0;
    bool trackOutcomes = true, trackPhase = false, trackSequence = false;
    int deathPenalty = 1, survivalReward = 1;
    bool coarseFallback = true, matchSubstep = false, matchIntervals = true;
    bool bridgeInterpolate = true;
    bool preferHybridFirst = true;
    float ucbCoefficient = 1.25f;
    float neighborBleed = .35f;
    int eligibilitySteps = 3;
    int positionBinX = 64, positionBinY = 48, tpsQuantum = 30, coarseAttempts = 6;
    int lineupDepth = 1;
    int observationTicks = 48;
    int minimumAttempts = 3;
    int maxUniqueKeys = 512;
    int maxActionsPerSpot = 4;
    int edges = 0;
    int observations = 0;
    int tried = 0;
    int confirmed = 0;
    int drift = 0;
    int verifyUpdates = 0;

private:
    void feedback(EbHybridObservation const& edge, int reward, int deathAge = -1,
                  int intervening = 0, bool survivedBaseline = false);
    int shapeReward(EbHybridObservation const& edge, int reward, int deathAge,
                    bool survivedBaseline) const;
    void pushTrace(EbHybridObservation const& edge);
    void creditEligibility(int tick);
    EbHybridObservation m_previous{};
    int m_intervening = 0;
    std::array<int, 2> m_sequences{};
    EbHybridModel m_model;
    std::filesystem::path m_path;
    std::array<EbHybridObservation, 2> m_edges{};
    std::array<int, 2> m_lastTick{-1, -1};
    std::array<int, 2> m_interval{-1, -1};
    std::array<int, 2> m_previousInterval{-1, -1};
    std::array<EbHybridObservation, 4> m_trace{};
    int m_traceWrite = 0;
    int m_traceCount = 0;
    bool m_pending = false;
    bool m_loaded = false;
};
#endif
