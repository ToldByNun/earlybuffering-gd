#ifndef NEURAL_LEARNING_HPP
#define NEURAL_LEARNING_HPP

#include "neural_features.hpp"
#include "neural_model.hpp"
#include "neural_replay.hpp"
#include <array>
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
    int prepare(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int tick,
                int limit, float dt, float firstDt, EbPlanOptions const& options);
    std::vector<int> rankedVerifyDelays(int limit) const;
    void noteVerifyOutcome(int delay, bool verifiedSafe);
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
    EbNeuralModel& model();
    EbNeuralModel const& model() const;
    EbNeuralReplay& replay();

    int topK = 2;
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
        float features[EbNeuralModel::kFeatureDim]{};

    private:
    };

    EbNeuralModel m_model;
    EbNeuralReplay m_replay;
    std::filesystem::path m_path;
    std::array<EdgeState, 2> m_edges{};
    bool m_loaded = false;
    bool m_hasPriorOrWeights = false;

    void clearEdges();
    bool tryLoadPrior();
    void fillFeatures(PlayLayer* layer, PlayerObject* player, bool down, bool player1, int limit,
                      float dt, float firstDt, float* outFeatures) const;
    static std::uint8_t channelBitsForObject(GameObject* object);
};

#endif
