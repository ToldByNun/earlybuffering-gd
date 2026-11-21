#ifndef NEURAL_MODEL_HPP
#define NEURAL_MODEL_HPP

#include <cstdint>
#include <filesystem>
#include <vector>

// Nova-inspired Candidate Scorer: 3512 → 128 → 64 → {49 rank | 1 sigmoid}.
// Scores rank which delays deserve verifyDelay budget; never arm from raw score.
class EbNeuralModel {
public:
    // Local env grid (Nova): 24×16×9 sparse channels + 56 continuous floats.
    static constexpr int kGridCols = 24;
    static constexpr int kGridRows = 16;
    static constexpr int kGridChannels = 9;
    static constexpr int kGridCells = kGridCols * kGridRows;
    static constexpr int kGridDim = kGridCells * kGridChannels;
    static constexpr int kContinuousDim = 56;
    static constexpr int kFeatureDim = kGridDim + kContinuousDim;
    static constexpr int kContinuousOffset = kGridDim;

    // Continuous slot for candidate delay encoding (Nova puts candidate params in floats).
    static constexpr int kCandidateDelayFeature =
        kContinuousOffset + 15; // delay / kMaxDelay ∈ [0,1]

    static constexpr int kHidden1 = 128;
    static constexpr int kHidden2 = 64;
    static constexpr int kActionCount = 49;
    static constexpr int kMaxDelay = kActionCount - 1;
    static constexpr float kMaskedScore = -1.0e30f;
    static constexpr float kDefaultTau = 0.05f;
    static constexpr float kDefaultGamma = 0.95f;
    static constexpr std::uint64_t kMaxFileBytes = 8ull * 1024ull * 1024ull;

    EbNeuralModel();

    void reset();
    void setFrozen(bool frozen);
    bool frozen() const;
    bool dirty() const;

    // EB-linear layout: continuous[15] = delay / kMaxDelay (legacy prior-compatible).
    static void writeCandidateDelay(float* features, int delay);
    // Nova-confirmed pack: continuous[15..17] action one-hot (0 idle / 1 press / 2 release),
    // continuous[18] = log1p(delay)*0.25. Clears [15..18] then writes.
    static void writeNovaCandidate(float* features, int delay, int actionKind /*0..2*/);
    // Dispatches LinearMax → writeCandidateDelay, Log1p → writeNovaCandidate(press?1:2).
    static void writeCandidateForPolicy(float* features, int delay, bool press, bool novaLog1p);

    // Online 49-way rank head → outScores[49]. Delays d > limit get kMaskedScore.
    void score(float const* features, int limit, float* outScores) const;
    // Nova-faithful single confidence ∈ [0,1] (sigmoid). Features should encode the candidate.
    float confidence(float const* features) const;
    // Descending score order among delays in [0, limit] via 49-way head.
    std::vector<int> rankedDelays(float const* features, int limit, int topK = kActionCount) const;
    // Rank by per-candidate sigmoid (copies features, writes delay slot each time).
    // novaLog1p: use Nova action-onehot + log1p(delay) slots instead of linear[15].
    std::vector<int> rankedDelaysByConfidence(float const* features, int limit,
                                              int topK = kActionCount,
                                              bool novaLog1p = false) const;

    void learnTd(float const* features, int action, float reward, float const* nextFeatures,
                 bool done, int nextLimit, float learningRate = 0.01f,
                 float gamma = kDefaultGamma);
    void learnRank(float const* features, int positiveDelay, int limit, float learningRate = 0.01f);
    // Push a failed/rejected delay down the 49-way head (pairs with learnConfidence=0).
    void learnRankReject(float const* features, int rejectDelay, int limit,
                         float learningRate = 0.01f);
    // Supervised BCE on sigmoid confidence head; target01 in [0,1].
    void learnConfidence(float const* features, float target01, float learningRate = 0.01f);

    void softUpdateTarget(float tau = kDefaultTau);
    void hardCopyTarget();

    bool save(std::filesystem::path const& path);
    bool load(std::filesystem::path const& path);

    static bool featuresFinite(float const* features);
    static int clampLimit(int limit);
    static float sigmoid(float value);

private:
    static constexpr int kW1 = kHidden1 * kFeatureDim;
    static constexpr int kB1 = kHidden1;
    static constexpr int kW2 = kHidden2 * kHidden1;
    static constexpr int kB2 = kHidden2;
    static constexpr int kW3 = kActionCount * kHidden2;
    static constexpr int kB3 = kActionCount;
    static constexpr int kWConf = kHidden2;
    static constexpr int kBConf = 1;
    static constexpr int kWeightCount = kW1 + kB1 + kW2 + kB2 + kW3 + kB3 + kWConf + kBConf;

    class LayerViews {
    public:
        float* w1 = nullptr;
        float* b1 = nullptr;
        float* w2 = nullptr;
        float* b2 = nullptr;
        float* w3 = nullptr;
        float* b3 = nullptr;
        float* wConf = nullptr;
        float* bConf = nullptr;

    private:
    };

    std::vector<float> m_online;
    std::vector<float> m_target;
    bool m_frozen = false;
    bool m_dirty = false;

    static LayerViews views(std::vector<float>& bank);
    static LayerViews constViews(std::vector<float> const& bank);
    static void initWeights(std::vector<float>& bank, std::uint32_t seed);
    static void forwardTrunk(LayerViews const& net, float const* features, float* hidden1,
                             float* hidden2);
    static void forwardRank(LayerViews const& net, float const* hidden2, float* logits);
    static float forwardConfidenceLogit(LayerViews const& net, float const* hidden2);
    static void applyMask(float* scores, int limit);
    static float relu(float value);
    void backpropFromHidden2(float const* features, float const* hidden1, float const* hidden2,
                             float const* gradHidden2, float learningRate);
    void backpropRank(float const* features, float const* hidden1, float const* hidden2,
                      float const* outputGrad, float learningRate);
    void backpropConfidence(float const* features, float const* hidden1, float const* hidden2,
                            float logitGrad, float learningRate);
    static std::uint64_t mix(std::uint64_t key, unsigned char byte);
};

#endif
