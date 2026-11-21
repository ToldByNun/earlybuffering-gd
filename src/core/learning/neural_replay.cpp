#include "neural_replay.hpp"
#include <cmath>
#include <cstring>
#include <limits>

EbNeuralReplay::EbNeuralReplay() : m_slots(static_cast<size_t>(kCapacity)) {
    clear();
}

void EbNeuralReplay::clear() {
    m_count = 0;
    m_next = 0;
}

int EbNeuralReplay::size() const {
    return m_count;
}

int EbNeuralReplay::capacity() const {
    return kCapacity;
}

bool EbNeuralReplay::empty() const {
    return m_count == 0;
}

bool EbNeuralReplay::full() const {
    return m_count >= kCapacity;
}

bool EbNeuralReplay::push(float const* state, int action, float reward, float const* next,
                          bool done) {
    if (!state || !next)
        return false;
    if (!EbNeuralModel::featuresFinite(state) || !EbNeuralModel::featuresFinite(next))
        return false;
    if (action < 0 || action > EbNeuralModel::kMaxDelay || !std::isfinite(reward))
        return false;

    Transition& slot = m_slots[static_cast<size_t>(m_next)];
    std::memcpy(slot.state, state, sizeof(float) * static_cast<size_t>(EbNeuralModel::kFeatureDim));
    std::memcpy(slot.next, next, sizeof(float) * static_cast<size_t>(EbNeuralModel::kFeatureDim));
    slot.action = action;
    slot.reward = reward;
    slot.done = done;
    m_next = (m_next + 1) % kCapacity;
    if (m_count < kCapacity)
        ++m_count;
    return true;
}

EbNeuralReplay::Transition const& EbNeuralReplay::at(int index) const {
    static Transition empty;
    if (index < 0 || index >= m_count)
        return empty;
    int const start = (m_next - m_count + kCapacity) % kCapacity;
    int const slot = (start + index) % kCapacity;
    return m_slots[static_cast<size_t>(slot)];
}

bool EbNeuralReplay::sample(Transition& out, std::uint32_t rngState) const {
    if (m_count <= 0)
        return false;
    std::uint32_t state = rngState ? rngState : 1u;
    state = state * 1664525u + 1013904223u;
    int const index = static_cast<int>(state % static_cast<std::uint32_t>(m_count));
    out = at(index);
    return true;
}

bool EbNeuralReplay::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    expect(kCapacity == 256, "capacity");

    EbNeuralReplay replay;
    expect(replay.empty() && replay.size() == 0, "empty");

    std::vector<float> state(static_cast<size_t>(EbNeuralModel::kFeatureDim), 0.1f);
    std::vector<float> next = state;
    next[0] = 0.2f;
    expect(replay.push(state.data(), 2, 1.0f, next.data(), false), "push0");
    expect(replay.size() == 1, "size1");

    state[3] = std::numeric_limits<float>::quiet_NaN();
    expect(!replay.push(state.data(), 1, 1.0f, next.data(), false), "reject-nan");
    expect(replay.size() == 1, "size-unchanged");

    state[3] = 0.0f;
    for (int step = 0; step < kCapacity + 10; ++step) {
        state[0] = static_cast<float>(step) * 0.001f;
        expect(replay.push(state.data(), step % 5, 0.5f, next.data(), step % 7 == 0), "fill");
    }
    expect(replay.full() && replay.size() == kCapacity, "full-ring");

    Transition sampled;
    expect(replay.sample(sampled, 42u), "sample");
    expect(sampled.action >= 0 && sampled.action <= EbNeuralModel::kMaxDelay, "sample-action");

    if (fails == 0) {
        report = "NeuralReplay OK";
        return true;
    }
    report = std::string("NeuralReplay fails: ") + report;
    return false;
}
