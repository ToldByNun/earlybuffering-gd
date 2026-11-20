#ifndef NEURAL_MODEL_HPP
#define NEURAL_MODEL_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// Fixed MLP Candidate Scorer: F → 64 → 64 → 49 scores.
// Scores rank which delays deserve verifyDelay budget; never arm from raw score.
class EbNeuralModel {
public:
    static constexpr int kFeatureDim = 48;
    static constexpr int kHiddenDim = 64;
    static constexpr int kActionCount = 49;
    static constexpr int kMaxDelay = kActionCount - 1;
    static constexpr float kMaskedScore = -1.0e30f;
    static constexpr float kDefaultTau = 0.05f;
    static constexpr float kDefaultGamma = 0.95f;
    static constexpr std::uint64_t kMaxFileBytes = 512ull * 1024ull;

    EbNeuralModel();

    void reset();
    void setFrozen(bool frozen);
    bool frozen() const;
    bool dirty() const;

    // Online forward → outScores[49]. Delays d > limit (and invalid inputs) get kMaskedScore.
    void score(float const* features, int limit, float* outScores) const;
    // Descending score order among delays in [0, limit]. Empty when features invalid.
    std::vector<int> rankedDelays(float const* features, int limit, int topK = kActionCount) const;

    // Online TD update on the scorer head (action masked to current limit).
    void learnTd(float const* features, int action, float reward, float const* nextFeatures,
                 bool done, int nextLimit, float learningRate = 0.01f,
                 float gamma = kDefaultGamma);
    // Supervised push: raise positiveDelay vs other legal delays (Candidate Scorer pretrain).
    void learnRank(float const* features, int positiveDelay, int limit, float learningRate = 0.01f);

    void softUpdateTarget(float tau = kDefaultTau);
    void hardCopyTarget();

    bool save(std::filesystem::path const& path);
    bool load(std::filesystem::path const& path);

    static bool featuresFinite(float const* features);
    static int clampLimit(int limit);

private:
    static constexpr int kW1 = kHiddenDim * kFeatureDim;
    static constexpr int kB1 = kHiddenDim;
    static constexpr int kW2 = kHiddenDim * kHiddenDim;
    static constexpr int kB2 = kHiddenDim;
    static constexpr int kW3 = kActionCount * kHiddenDim;
    static constexpr int kB3 = kActionCount;
    static constexpr int kWeightCount = kW1 + kB1 + kW2 + kB2 + kW3 + kB3;

    class LayerViews {
    public:
        float* w1 = nullptr;
        float* b1 = nullptr;
        float* w2 = nullptr;
        float* b2 = nullptr;
        float* w3 = nullptr;
        float* b3 = nullptr;

    private:
    };

    using WeightBank = std::array<float, kWeightCount>;

    WeightBank m_online{};
    WeightBank m_target{};
    bool m_frozen = false;
    bool m_dirty = false;

    static LayerViews views(WeightBank& bank);
    static LayerViews constViews(WeightBank const& bank);
    static void initWeights(WeightBank& bank, std::uint32_t seed);
    static void forward(LayerViews const& net, float const* features, float* hidden1,
                        float* hidden2, float* logits);
    static void applyMask(float* scores, int limit);
    static float relu(float value);
    void backpropOutput(float const* features, float const* hidden1, float const* hidden2,
                        float const* outputGrad, float learningRate);
    static std::uint64_t mix(std::uint64_t key, unsigned char byte);
};

#endif
