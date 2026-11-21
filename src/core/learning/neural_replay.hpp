#ifndef NEURAL_REPLAY_HPP
#define NEURAL_REPLAY_HPP

#include "neural_model.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Fixed-capacity ring of scorer transitions (state, action, reward, next, done).
class EbNeuralReplay {
public:
    static constexpr int kCapacity = 256;

    class Transition {
    public:
        float state[EbNeuralModel::kFeatureDim]{};
        float next[EbNeuralModel::kFeatureDim]{};
        int action = 0;
        float reward = 0.0f;
        bool done = false;

    private:
    };

    EbNeuralReplay();

    void clear();
    int size() const;
    int capacity() const;
    bool empty() const;
    bool full() const;

    // Push copies feature vectors; rejects NaN/Inf rows.
    bool push(float const* state, int action, float reward, float const* next, bool done);
    Transition const& at(int index) const;
    // Uniform sample into out; returns false when empty.
    bool sample(Transition& out, std::uint32_t rngState) const;

    static bool selfCheck(std::string& report);

private:
    std::vector<Transition> m_slots;
    int m_count = 0;
    int m_next = 0;
};

#endif
