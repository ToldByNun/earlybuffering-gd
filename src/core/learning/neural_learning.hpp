#ifndef NEURAL_LEARNING_HPP
#define NEURAL_LEARNING_HPP

#include "neural_features.hpp"
#include "neural_model.hpp"
#include "neural_replay.hpp"
#include "neural_viz_snapshot.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;
class GameObject;
class EbPlanOptions;
class EbSimPlan;

// Player-driven neural Candidate Scorer lifecycle (mirrors EbHybridLearning hooks).
// Train: AssistMode::AI && Auto-Tune && !frozen. Infer: AI with loaded weights/prior.
class EbNeuralLearning {
public:
    static EbNeuralLearning& get();
    // Mode is AI (may infer and/or train).
    static bool active();
    // Mode AI + Auto-Tune (writes allowed when not frozen).
    static bool training();

    void open(std::filesystem::path path);
    void close();
    void reset();
    void save();
    void settingsChanged();
    void boundary(bool newAttempt);
    void edge(bool player1, int tick, bool down);
    // Builds features; returns top ranked delay hint or -1.
    // withTrajectoryPair defaults off — Live click must stay µs-cheap (no dual Shadow).
    int prepare(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int tick,
                int limit, float dt, float firstDt, EbPlanOptions const& options,
                bool withTrajectoryPair = false);
    std::vector<int> rankedVerifyDelays(int limit) const;
    // Sigmoid confidence for a candidate delay using the last prepare features.
    float confidenceForDelay(int delay) const;
    // Skip AI-first when frame remUs is below this (400 desktop / 250 constrained).
    static int minRemUsForAiFirst();
    void noteVerifyOutcome(int delay, bool verifiedSafe);
    void noteArmed(int delay, float confidence);
    void vanilla(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int tick,
                 float dt);
    void decision(bool player1, EbSimPlan const& plan);
    void fired(bool player1, int planId, int delay, float phase);
    void poll(PlayLayer* layer, int tick);
    void death(bool player1, int tick);
    void freeze(bool value);
    bool frozen() const;
    bool loaded() const;
    bool hasUsableWeights() const;
    // Load user base prior (saveDir) or packaged prior.ebnn into memory; write to m_path.
    bool loadPrior();
    // Persist current weights as the cross-level user base prior (saveDir/ai/prior.ebnn).
    bool saveAsBasePrior();
    // When true, open()/save() use the shared user base prior instead of per-level .neural.
    void setTrainBasePrior(bool on);
    bool trainBasePrior() const {
        return m_trainBasePrior;
    }
    static std::filesystem::path userPriorPath();

    EbNeuralModel& model();
    EbNeuralModel const& model() const;
    EbNeuralReplay& replay();
    // Batch-train from the IL/RL session buffer (player demos + autoplay).
    // persist=false skips disk save (use on reset/idle; manual Train Session persists).
    int trainSession(int passes = 4, bool persist = true);
    // Train only newly pushed samples (capped). Safe for resetAttempt.
    int trainSessionIfGrown(int passes = 1, int maxUpdates = 48);
    // Run deferred death train (called from attempt boundary, never destroyPlayer).
    void flushDeferredTrain();
    // Rebuild local grid + pull last ranked delays for Visualize AI (cheap vs prepare).
    void refreshViz(PlayLayer* layer);
    EbNeuralVizSnapshot const& viz() const;

    bool preferAiFirst = true;
    // 4 gives room for delay>0 after d=0 in the verify set (AI-first prefers delay>0).
    int topK = 4;
    float learningRate = 0.01f;
    int observationTicks = 48;
    int edges = 0;
    int inferences = 0;
    int verifies = 0;
    int verifyHits = 0;
    int updates = 0;
    int deaths = 0;
    int tried = 0;
    int confirmed = 0;
    int lastArmedDelay = -1;
    float lastArmedConfidence = 0.0f;

    static bool selfCheck(std::string& report);

private:
    class EdgeState {
    public:
        bool valid = false;
        bool down = true;
        bool player1 = true;
        int tick = 0;
        int limit = 0;
        int hint = -1;
        int plannedDelay = -1;
        int planId = -1;
        bool fired = false;
        bool pending = false;
        float startX = 0.0f;
        float features[EbNeuralModel::kFeatureDim]{};
        // Blended AI ± Hybrid verify order from last prepare (AP06).
        std::vector<int> verifyOrder;

    private:
    };

    class RecentEdge {
    public:
        bool valid = false;
        int tick = 0;
        int action = 0;
        int limit = 0;
        float startX = 0.0f;
        float features[EbNeuralModel::kFeatureDim]{};

    private:
    };

    EbNeuralModel m_model;
    EbNeuralReplay m_replay;
    EbNeuralVizSnapshot m_viz;
    std::filesystem::path m_path;
    std::array<EdgeState, 2> m_edges{};
    std::array<RecentEdge, 4> m_recent{};
    int m_recentNext = 0;
    int m_sessionTrainedThrough = 0;
    bool m_loaded = false;
    bool m_hasPriorOrWeights = false;
    bool m_trainBasePrior = false;
    bool m_deferTrain = false;
    int m_savesSincePersist = 0;

    void clearEdges();
    bool tryLoadPrior();
    bool tryLoadPackagedPrior();
    void openSharedBase();
    void softResetAttemptState();
    // withTrajectoryPair: dual Shadow idle/action probe — Live click path must pass false
    // (research_20261122_* hitches). Autoplay/route may enable under its own µs wall.
    void fillFeatures(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int limit,
                      float dt, float firstDt, float* outFeatures, bool withTrajectoryPair = false);
    // Cheap idle-coast vs action-now shadow pair → geometry[3..10] + firstDeathHintNorm.
    static void fillTrajectoryPair(PlayLayer* layer, PlayerObject* player, bool down, int limit,
                                   float dt, float firstDt, EbNeuralFeatureState& state);
    // storeVizHitboxes: only for Visualize AI (skip on click prepare — cheaper).
    void captureGrid(PlayLayer* layer, PlayerObject* player, EbNeuralGrid& outGrid, float& originX,
                     float& originY, bool storeVizHitboxes);
    void publishVizRanks();
    void rememberRecent(EdgeState const& edge);
    void punishRecent(int deathTick, float endX);
    static float progressNorm(float startX, float endX);
    static float successReward(int plannedDelay, float progress);
    static float deathReward(float progress);
    // Evidence tiers for IL/RL labels (weight teacher ≪ verify ≪ survived).
    enum class Evidence : std::uint8_t {
        TeacherSuggested = 0,
        SimulationVerified = 1,
        SurvivedObserved = 2,
        Death = 3
    };
    static float evidenceLearnScale(Evidence evidence);
    static float evidenceRewardScale(Evidence evidence);
    static float evidenceConfidenceTarget(Evidence evidence, int plannedDelay, float progress);
    static std::uint8_t channelBitsForObject(GameObject* object);
};

#endif
