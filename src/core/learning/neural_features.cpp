#include "neural_features.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

float EbNeuralFeatures::clampUnit(float value, float lo, float hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

void EbNeuralFeatures::clear(float* outFeatures) {
    if (!outFeatures)
        return;
    for (int index = 0; index < EbNeuralModel::kFeatureDim; ++index)
        outFeatures[index] = 0.0f;
}

void EbNeuralFeatures::writeContinuous(float* outFeatures, EbNeuralFeatureState const& state) {
    if (!outFeatures)
        return;
    float* continuous = outFeatures + EbNeuralModel::kContinuousOffset;
    for (int index = 0; index < EbNeuralModel::kContinuousDim; ++index)
        continuous[index] = 0.0f;

    int const mode = std::clamp(state.modeKind, 0, 7);
    continuous[mode] = 1.0f;
    continuous[8] = state.press ? 1.0f : 0.0f;
    continuous[9] = state.player1 ? 1.0f : 0.0f;
    continuous[10] = state.dual ? 1.0f : 0.0f;
    continuous[11] = state.practice ? 1.0f : 0.0f;
    continuous[12] = state.grounded ? 1.0f : 0.0f;
    continuous[13] = clampUnit(state.yNorm, -3.0f, 3.0f);
    continuous[14] = clampUnit(state.vxSign, -1.0f, 1.0f);
    // Index 15 reserved for candidate delay (EbNeuralModel::kCandidateDelayFeature).
    EbNeuralModel::writeCandidateDelay(outFeatures, state.candidateDelay);
    continuous[16] = clampUnit(state.limitNorm, 0.0f, 1.0f);
    continuous[17] = clampUnit(state.remainingBudgetNorm, 0.0f, 1.0f);
    continuous[18] = clampUnit(state.xFrac, 0.0f, 1.0f);
    continuous[19] = clampUnit(state.vyNorm, -3.0f, 3.0f);
    continuous[20] = clampUnit(state.speedNorm, 0.0f, 3.0f);
    continuous[21] = clampUnit(state.sizeNorm, 0.0f, 3.0f);
    continuous[22] = clampUnit(state.gravityMod, -3.0f, 3.0f);
    continuous[23] = state.upsideDown ? 1.0f : 0.0f;
    continuous[24] = state.goingLeft ? 1.0f : 0.0f;
    continuous[25] = state.sideways ? 1.0f : 0.0f;
    continuous[26] = state.firstDeathHintNorm < 0.0f
                         ? -1.0f
                         : clampUnit(state.firstDeathHintNorm, 0.0f, 3.0f);
    continuous[27] = state.nearOrb ? 1.0f : 0.0f;
    for (int index = 0; index < 28; ++index)
        continuous[28 + index] = clampUnit(state.geometry[index], -3.0f, 3.0f);
}

void EbNeuralFeatures::build(float* outFeatures, EbNeuralGrid const& grid,
                             EbNeuralFeatureState const& state) {
    if (!outFeatures)
        return;
    clear(outFeatures);
    grid.scatterTo(outFeatures);
    writeContinuous(outFeatures, state);
}

bool EbNeuralFeatures::rejectNonFinite(float const* features) {
    return !EbNeuralModel::featuresFinite(features);
}

bool EbNeuralFeatures::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    expect(kVersion == 1, "version");
    expect(EbNeuralModel::kFeatureDim == EbNeuralModel::kGridDim + EbNeuralModel::kContinuousDim,
           "dim-split");
    expect(EbNeuralModel::kCandidateDelayFeature == EbNeuralModel::kContinuousOffset + 15,
           "candidate-slot");

    EbNeuralGrid grid;
    EbNeuralGridObject block;
    block.x = 50.0f;
    block.y = 80.0f;
    block.width = 20.0f;
    block.height = 20.0f;
    block.channelBits = EbNeuralGrid::kChannelSolid;
    grid.rasterize(50.0f, 80.0f, &block, 1);

    EbNeuralFeatureState state;
    state.modeKind = 4;
    state.press = true;
    state.limit = 8;
    state.limitNorm = 8.0f / 48.0f;
    state.candidateDelay = 3;
    state.yNorm = 0.25f;
    state.geometry[0] = 1.5f;

    std::vector<float> features(static_cast<size_t>(EbNeuralModel::kFeatureDim), 0.0f);
    build(features.data(), grid, state);
    expect(!rejectNonFinite(features.data()), "finite-ok");
    expect(features[EbNeuralModel::kContinuousOffset + 4] == 1.0f, "mode-onehot");
    expect(features[EbNeuralModel::kCandidateDelayFeature] == 3.0f / 48.0f, "candidate-encoded");
    expect(features[EbNeuralModel::kContinuousOffset + 28] == 1.5f, "geometry0");

    int gridLit = 0;
    for (int index = 0; index < EbNeuralModel::kGridDim; ++index) {
        if (features[static_cast<size_t>(index)] != 0.0f)
            ++gridLit;
    }
    expect(gridLit > 0, "grid-packed");

    features[0] = std::numeric_limits<float>::quiet_NaN();
    expect(rejectNonFinite(features.data()), "reject-nan");

    // Dim stable across two builds.
    std::vector<float> again(static_cast<size_t>(EbNeuralModel::kFeatureDim), 0.0f);
    features[0] = 0.0f;
    build(features.data(), grid, state);
    build(again.data(), grid, state);
    bool same = true;
    for (int index = 0; index < EbNeuralModel::kFeatureDim; ++index)
        same = same && features[static_cast<size_t>(index)] == again[static_cast<size_t>(index)];
    expect(same, "build-stable");

    if (fails == 0) {
        report = "NeuralFeatures OK";
        return true;
    }
    report = std::string("NeuralFeatures fails: ") + report;
    return false;
}
