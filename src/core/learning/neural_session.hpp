#ifndef NEURAL_SESSION_HPP
#define NEURAL_SESSION_HPP

#include "neural_model.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// IL / offline RL buffer: every attempt stores scorer transitions; Train Session
// replays them into the model (player demos + autoplay outcomes).
class EbNeuralSession {
public:
    static constexpr int kCapacity = 4096;

    class Sample {
    public:
        float state[EbNeuralModel::kFeatureDim]{};
        float next[EbNeuralModel::kFeatureDim]{};
        int action = 0;
        int limit = 0;
        float reward = 0.0f;
        bool done = false;

    private:
    };

    static EbNeuralSession& get();

    void clear() {
        m_samples.assign(static_cast<size_t>(kCapacity), Sample{});
        m_count = 0;
        m_next = 0;
        m_attempts = 0;
        m_lastTrainUpdates = 0;
        m_attemptOpen = false;
    }
    void beginAttempt() {
        m_attemptOpen = true;
    }
    void endAttempt() {
        if (m_attemptOpen)
            ++m_attempts;
        m_attemptOpen = false;
    }
    // Push a labeled transition (reject NaN). Survives across attempts until clear/train.
    bool push(float const* state, int action, int limit, float reward, float const* next,
              bool done) {
        if (!state || !next || action < 0 || action > EbNeuralModel::kMaxDelay)
            return false;
        if (!EbNeuralModel::featuresFinite(state) || !EbNeuralModel::featuresFinite(next))
            return false;
        if (m_samples.empty())
            m_samples.assign(static_cast<size_t>(kCapacity), Sample{});
        Sample& slot = m_samples[static_cast<size_t>(m_next)];
        std::memcpy(slot.state, state, sizeof(slot.state));
        std::memcpy(slot.next, next, sizeof(slot.next));
        slot.action = action;
        slot.limit = EbNeuralModel::clampLimit(limit);
        slot.reward = reward;
        slot.done = done;
        m_next = (m_next + 1) % kCapacity;
        if (m_count < kCapacity)
            ++m_count;
        return true;
    }

    int size() const {
        return m_count;
    }
    int attemptCount() const {
        return m_attempts;
    }
    int lastTrainUpdates() const {
        return m_lastTrainUpdates;
    }

    // Batch IL/RL update into model. Returns weight-update count.
    int train(EbNeuralModel& model, float learningRate, int passes = 4) {
        if (m_count <= 0 || passes <= 0)
            return 0;
        float const rate = std::clamp(learningRate, 0.0001f, 0.1f);
        int updates = 0;
        for (int pass = 0; pass < passes; ++pass) {
            for (int index = 0; index < m_count; ++index) {
                int const slot = (m_next - m_count + index + kCapacity * 2) % kCapacity;
                Sample const& sample = m_samples[static_cast<size_t>(slot)];
                model.learnTd(sample.state, sample.action, sample.reward, sample.next, sample.done,
                              sample.limit, rate);
                if (sample.reward > 0.0f)
                    model.learnRank(sample.state, sample.action, sample.limit, rate);
                float scratch[EbNeuralModel::kFeatureDim];
                std::memcpy(scratch, sample.state, sizeof(scratch));
                EbNeuralModel::writeCandidateDelay(scratch, sample.action);
                float const target = sample.reward > 0.0f ? 1.0f : (sample.done ? 0.0f : 0.5f);
                model.learnConfidence(scratch, target, rate);
                ++updates;
            }
        }
        m_lastTrainUpdates = updates;
        return updates;
    }

    static bool selfCheck(std::string& report) {
        int fails = 0;
        auto expect = [&](bool ok, char const* name) {
            if (!ok) {
                ++fails;
                report += std::string("FAIL ") + name + "; ";
            }
        };
        EbNeuralSession session;
        session.clear();
        expect(session.size() == 0, "empty");
        float state[EbNeuralModel::kFeatureDim]{};
        float next[EbNeuralModel::kFeatureDim]{};
        state[0] = 0.25f;
        next[0] = 0.5f;
        expect(session.push(state, 3, 12, 1.0f, next, false), "push");
        expect(session.size() == 1, "size-1");
        session.beginAttempt();
        session.endAttempt();
        expect(session.attemptCount() == 1, "attempt");
        EbNeuralModel model;
        model.reset();
        expect(session.train(model, 0.01f, 1) > 0, "train");
        if (fails == 0) {
            report = "NeuralSession OK";
            return true;
        }
        report = std::string("NeuralSession fails: ") + report;
        return false;
    }

private:
    friend class EbServiceStore;

    EbNeuralSession() = default;

    std::vector<Sample> m_samples;
    int m_count = 0;
    int m_next = 0;
    int m_attempts = 0;
    int m_lastTrainUpdates = 0;
    bool m_attemptOpen = false;
};

#endif
