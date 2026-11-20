#include "neural_model.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

EbNeuralModel::EbNeuralModel() {
    reset();
}

void EbNeuralModel::reset() {
    initWeights(m_online, 0xEB001848u);
    hardCopyTarget();
    m_frozen = false;
    m_dirty = true;
}

void EbNeuralModel::setFrozen(bool frozen) {
    m_frozen = frozen;
}

bool EbNeuralModel::frozen() const {
    return m_frozen;
}

bool EbNeuralModel::dirty() const {
    return m_dirty;
}

EbNeuralModel::LayerViews EbNeuralModel::views(WeightBank& bank) {
    LayerViews out;
    float* cursor = bank.data();
    out.w1 = cursor;
    cursor += kW1;
    out.b1 = cursor;
    cursor += kB1;
    out.w2 = cursor;
    cursor += kW2;
    out.b2 = cursor;
    cursor += kB2;
    out.w3 = cursor;
    cursor += kW3;
    out.b3 = cursor;
    return out;
}

EbNeuralModel::LayerViews EbNeuralModel::constViews(WeightBank const& bank) {
    return views(const_cast<WeightBank&>(bank));
}

void EbNeuralModel::initWeights(WeightBank& bank, std::uint32_t seed) {
    std::uint32_t state = seed ? seed : 1u;
    auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        float unit = static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
        return unit * 2.0f - 1.0f;
    };
    LayerViews net = views(bank);
    float const scale1 = 0.15f / std::sqrt(static_cast<float>(kFeatureDim));
    float const scale2 = 0.15f / std::sqrt(static_cast<float>(kHiddenDim));
    float const scale3 = 0.15f / std::sqrt(static_cast<float>(kHiddenDim));
    for (int index = 0; index < kW1; ++index)
        net.w1[index] = next() * scale1;
    for (int index = 0; index < kB1; ++index)
        net.b1[index] = 0.0f;
    for (int index = 0; index < kW2; ++index)
        net.w2[index] = next() * scale2;
    for (int index = 0; index < kB2; ++index)
        net.b2[index] = 0.0f;
    for (int index = 0; index < kW3; ++index)
        net.w3[index] = next() * scale3;
    for (int index = 0; index < kB3; ++index)
        net.b3[index] = 0.0f;
}

float EbNeuralModel::relu(float value) {
    return value > 0.0f ? value : 0.0f;
}

bool EbNeuralModel::featuresFinite(float const* features) {
    if (!features)
        return false;
    for (int index = 0; index < kFeatureDim; ++index) {
        if (!std::isfinite(features[index]))
            return false;
    }
    return true;
}

int EbNeuralModel::clampLimit(int limit) {
    if (limit < 0)
        return 0;
    if (limit > kMaxDelay)
        return kMaxDelay;
    return limit;
}

void EbNeuralModel::forward(LayerViews const& net, float const* features, float* hidden1,
                            float* hidden2, float* logits) {
    for (int row = 0; row < kHiddenDim; ++row) {
        float sum = net.b1[row];
        float const* weights = net.w1 + row * kFeatureDim;
        for (int col = 0; col < kFeatureDim; ++col)
            sum += weights[col] * features[col];
        hidden1[row] = relu(sum);
    }
    for (int row = 0; row < kHiddenDim; ++row) {
        float sum = net.b2[row];
        float const* weights = net.w2 + row * kHiddenDim;
        for (int col = 0; col < kHiddenDim; ++col)
            sum += weights[col] * hidden1[col];
        hidden2[row] = relu(sum);
    }
    for (int row = 0; row < kActionCount; ++row) {
        float sum = net.b3[row];
        float const* weights = net.w3 + row * kHiddenDim;
        for (int col = 0; col < kHiddenDim; ++col)
            sum += weights[col] * hidden2[col];
        logits[row] = sum;
    }
}

void EbNeuralModel::applyMask(float* scores, int limit) {
    int const clamped = clampLimit(limit);
    for (int delay = clamped + 1; delay < kActionCount; ++delay)
        scores[delay] = kMaskedScore;
}

void EbNeuralModel::score(float const* features, int limit, float* outScores) const {
    if (!outScores)
        return;
    if (!featuresFinite(features)) {
        for (int delay = 0; delay < kActionCount; ++delay)
            outScores[delay] = kMaskedScore;
        return;
    }
    std::array<float, kHiddenDim> hidden1{};
    std::array<float, kHiddenDim> hidden2{};
    forward(constViews(m_online), features, hidden1.data(), hidden2.data(), outScores);
    applyMask(outScores, limit);
}

std::vector<int> EbNeuralModel::rankedDelays(float const* features, int limit, int topK) const {
    std::vector<int> ranked;
    if (!featuresFinite(features) || topK <= 0)
        return ranked;
    int const clamped = clampLimit(limit);
    std::array<float, kActionCount> scores{};
    score(features, clamped, scores.data());
    ranked.reserve(static_cast<size_t>(clamped + 1));
    for (int delay = 0; delay <= clamped; ++delay)
        ranked.push_back(delay);
    std::stable_sort(ranked.begin(), ranked.end(), [&](int left, int right) {
        if (scores[left] != scores[right])
            return scores[left] > scores[right];
        return left < right;
    });
    if (topK < static_cast<int>(ranked.size()))
        ranked.resize(static_cast<size_t>(topK));
    return ranked;
}

void EbNeuralModel::backpropOutput(float const* features, float const* hidden1,
                                   float const* hidden2, float const* outputGrad,
                                   float learningRate) {
    if (m_frozen || learningRate <= 0.0f)
        return;
    LayerViews net = views(m_online);
    std::array<float, kHiddenDim> gradHidden2{};
    for (int row = 0; row < kActionCount; ++row) {
        float const grad = outputGrad[row];
        if (grad == 0.0f)
            continue;
        float* weights = net.w3 + row * kHiddenDim;
        for (int col = 0; col < kHiddenDim; ++col) {
            gradHidden2[col] += grad * weights[col];
            weights[col] -= learningRate * grad * hidden2[col];
        }
        net.b3[row] -= learningRate * grad;
    }
    std::array<float, kHiddenDim> gradHidden1{};
    for (int row = 0; row < kHiddenDim; ++row) {
        float const gate = hidden2[row] > 0.0f ? 1.0f : 0.0f;
        float const gated = gradHidden2[row] * gate;
        float* weights = net.w2 + row * kHiddenDim;
        for (int col = 0; col < kHiddenDim; ++col) {
            gradHidden1[col] += gated * weights[col];
            weights[col] -= learningRate * gated * hidden1[col];
        }
        net.b2[row] -= learningRate * gated;
    }
    for (int row = 0; row < kHiddenDim; ++row) {
        float const gate = hidden1[row] > 0.0f ? 1.0f : 0.0f;
        float const gated = gradHidden1[row] * gate;
        float* weights = net.w1 + row * kFeatureDim;
        for (int col = 0; col < kFeatureDim; ++col)
            weights[col] -= learningRate * gated * features[col];
        net.b1[row] -= learningRate * gated;
    }
    m_dirty = true;
}

void EbNeuralModel::learnTd(float const* features, int action, float reward,
                            float const* nextFeatures, bool done, int nextLimit,
                            float learningRate, float gamma) {
    if (m_frozen || !featuresFinite(features))
        return;
    if (action < 0 || action > kMaxDelay)
        return;
    std::array<float, kHiddenDim> hidden1{};
    std::array<float, kHiddenDim> hidden2{};
    std::array<float, kActionCount> onlineScores{};
    forward(views(m_online), features, hidden1.data(), hidden2.data(), onlineScores.data());

    float bootstrap = 0.0f;
    if (!done && featuresFinite(nextFeatures)) {
        std::array<float, kHiddenDim> nextH1{};
        std::array<float, kHiddenDim> nextH2{};
        std::array<float, kActionCount> targetScores{};
        forward(constViews(m_target), nextFeatures, nextH1.data(), nextH2.data(),
                targetScores.data());
        applyMask(targetScores.data(), nextLimit);
        int const nextClamped = clampLimit(nextLimit);
        bootstrap = targetScores[0];
        for (int delay = 1; delay <= nextClamped; ++delay)
            bootstrap = std::max(bootstrap, targetScores[delay]);
        if (!(bootstrap > kMaskedScore * 0.5f))
            bootstrap = 0.0f;
    }
    float const targetValue = reward + (done ? 0.0f : gamma * bootstrap);
    float const prediction = onlineScores[action];
    std::array<float, kActionCount> outputGrad{};
    outputGrad[action] = prediction - targetValue;
    backpropOutput(features, hidden1.data(), hidden2.data(), outputGrad.data(), learningRate);
    softUpdateTarget();
}

void EbNeuralModel::learnRank(float const* features, int positiveDelay, int limit,
                              float learningRate) {
    if (m_frozen || !featuresFinite(features))
        return;
    int const clamped = clampLimit(limit);
    if (positiveDelay < 0 || positiveDelay > clamped)
        return;
    std::array<float, kHiddenDim> hidden1{};
    std::array<float, kHiddenDim> hidden2{};
    std::array<float, kActionCount> scores{};
    forward(views(m_online), features, hidden1.data(), hidden2.data(), scores.data());
    applyMask(scores.data(), clamped);

    std::array<float, kActionCount> outputGrad{};
    float const positive = scores[positiveDelay];
    int negatives = 0;
    float negativeSum = 0.0f;
    for (int delay = 0; delay <= clamped; ++delay) {
        if (delay == positiveDelay)
            continue;
        negativeSum += scores[delay];
        ++negatives;
    }
    if (negatives == 0)
        return;
    float const meanNegative = negativeSum / static_cast<float>(negatives);
    // Softplus-style pairwise: push positive up, pull mean negative down.
    float const margin = meanNegative + 0.5f - positive;
    if (margin <= 0.0f)
        return;
    outputGrad[positiveDelay] = -margin;
    float const share = margin / static_cast<float>(negatives);
    for (int delay = 0; delay <= clamped; ++delay) {
        if (delay == positiveDelay)
            continue;
        outputGrad[delay] = share;
    }
    backpropOutput(features, hidden1.data(), hidden2.data(), outputGrad.data(), learningRate);
    softUpdateTarget();
}

void EbNeuralModel::softUpdateTarget(float tau) {
    if (tau <= 0.0f)
        return;
    if (tau >= 1.0f) {
        hardCopyTarget();
        return;
    }
    float const keep = 1.0f - tau;
    for (int index = 0; index < kWeightCount; ++index)
        m_target[index] = keep * m_target[index] + tau * m_online[index];
}

void EbNeuralModel::hardCopyTarget() {
    m_target = m_online;
}

std::uint64_t EbNeuralModel::mix(std::uint64_t key, unsigned char byte) {
    return (key ^ byte) * 1099511628211ull;
}

bool EbNeuralModel::save(std::filesystem::path const& path) {
    if (!m_dirty)
        return true;
    std::int32_t const featureDim = kFeatureDim;
    std::int32_t const hiddenDim = kHiddenDim;
    std::int32_t const actionCount = kActionCount;
    std::uint32_t const flags = m_frozen ? 1u : 0u;
    std::ostringstream payload;
    payload.write(reinterpret_cast<char const*>(&featureDim), sizeof(featureDim));
    payload.write(reinterpret_cast<char const*>(&hiddenDim), sizeof(hiddenDim));
    payload.write(reinterpret_cast<char const*>(&actionCount), sizeof(actionCount));
    payload.write(reinterpret_cast<char const*>(&flags), sizeof(flags));
    payload.write(reinterpret_cast<char const*>(m_online.data()),
                  sizeof(float) * static_cast<size_t>(kWeightCount));
    payload.write(reinterpret_cast<char const*>(m_target.data()),
                  sizeof(float) * static_cast<size_t>(kWeightCount));
    std::string const body = payload.str();
    std::uint64_t checksum = 1;
    for (unsigned char byte : body)
        checksum = mix(checksum, byte);

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "EBNN1 " << checksum << '\n';
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    output.close();
    if (!output)
        return false;
    std::filesystem::rename(temporary, path, error);
    m_dirty = bool(error);
    return !error;
}

bool EbNeuralModel::load(std::filesystem::path const& path) {
    std::error_code error;
    auto const size = std::filesystem::file_size(path, error);
    if (error || size > kMaxFileBytes)
        return false;
    std::ifstream input(path, std::ios::binary);
    std::string version;
    std::uint64_t expected = 0;
    if (!(input >> version >> expected) || version != "EBNN1" || input.get() != '\n')
        return false;
    std::string body{std::istreambuf_iterator<char>(input), {}};
    std::uint64_t checksum = 1;
    for (unsigned char byte : body)
        checksum = mix(checksum, byte);
    if (checksum != expected)
        return false;
    if (body.size() < sizeof(std::int32_t) * 3 + sizeof(std::uint32_t))
        return false;

    std::int32_t featureDim = 0;
    std::int32_t hiddenDim = 0;
    std::int32_t actionCount = 0;
    std::uint32_t flags = 0;
    char const* cursor = body.data();
    char const* end = body.data() + body.size();
    auto readPod = [&](auto& value) {
        if (cursor + sizeof(value) > end)
            return false;
        std::memcpy(&value, cursor, sizeof(value));
        cursor += sizeof(value);
        return true;
    };
    if (!readPod(featureDim) || !readPod(hiddenDim) || !readPod(actionCount) || !readPod(flags))
        return false;
    if (featureDim != kFeatureDim || hiddenDim != kHiddenDim || actionCount != kActionCount)
        return false;
    size_t const weightBytes = sizeof(float) * static_cast<size_t>(kWeightCount);
    if (static_cast<size_t>(end - cursor) != weightBytes * 2)
        return false;

    WeightBank online{};
    WeightBank target{};
    std::memcpy(online.data(), cursor, weightBytes);
    cursor += weightBytes;
    std::memcpy(target.data(), cursor, weightBytes);
    for (float weight : online) {
        if (!std::isfinite(weight))
            return false;
    }
    for (float weight : target) {
        if (!std::isfinite(weight))
            return false;
    }
    m_online = online;
    m_target = target;
    m_frozen = (flags & 1u) != 0u;
    m_dirty = false;
    return true;
}
