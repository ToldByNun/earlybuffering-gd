#ifndef NEURAL_SESSION_HPP
#define NEURAL_SESSION_HPP

#include "neural_model.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// IL / offline RL buffer: every attempt stores scorer transitions; Train Session
// replays them into the model (player demos + autoplay outcomes).
class EbNeuralSession {
public:
    static constexpr int kCapacity = 4096;
    static constexpr std::uint32_t kFileMagic = 0x534E4245u; // "EBNS" LE
    static constexpr std::uint32_t kFileVersion = 1;

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
        m_persistedThrough = 0;
        m_lastPersistPath.clear();
        m_filesSaved = 0;
    }
    void beginAttempt() {
        m_attemptOpen = true;
    }
    void endAttempt() {
        if (m_attemptOpen)
            ++m_attempts;
        m_attemptOpen = false;
        maybePersistAttempt();
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
    int filesSaved() const {
        return m_filesSaved;
    }
    std::string const& lastPersistPath() const {
        return m_lastPersistPath;
    }

    // Dev/AI: write new samples since last persist under saveDir/ai/sessions/.
    bool maybePersistAttempt();
    // Write [start, end) logical samples to path. Returns false on I/O error.
    bool saveRange(std::filesystem::path const& path, int startIndex, int endIndex) const;
    // Append samples from a .ebns file into the ring buffer. Returns added count.
    int loadFile(std::filesystem::path const& path);
    // Load every *.ebns under dir (newest first, capacity-capped). Returns added count.
    int loadDirectory(std::filesystem::path const& dir);
    static std::filesystem::path sessionsDir();

    // Batch IL/RL update. startIndex/endIndex are logical buffer indices [0, size).
    // maxUpdates caps work (death/reset must stay hitch-free).
    int train(EbNeuralModel& model, float learningRate, int passes = 4, int startIndex = 0,
              int endIndex = -1, int maxUpdates = -1) {
        if (m_count <= 0 || passes <= 0)
            return 0;
        int const end = endIndex < 0 ? m_count : std::min(endIndex, m_count);
        int const start = std::clamp(startIndex, 0, end);
        if (start >= end)
            return 0;
        float const rate = std::clamp(learningRate, 0.0001f, 0.1f);
        int updates = 0;
        for (int pass = 0; pass < passes; ++pass) {
            for (int index = start; index < end; ++index) {
                if (maxUpdates >= 0 && updates >= maxUpdates) {
                    m_lastTrainUpdates = updates;
                    return updates;
                }
                int const slot = (m_next - m_count + index + kCapacity * 2) % kCapacity;
                Sample const& sample = m_samples[static_cast<size_t>(slot)];
                // Identical next with !done = supervised label without a real s'
                // (Problem C). Death (done) still uses TD with bootstrap=0.
                bool identicalNext = true;
                for (int feature = 0; feature < EbNeuralModel::kFeatureDim; ++feature) {
                    if (sample.state[feature] != sample.next[feature]) {
                        identicalNext = false;
                        break;
                    }
                }
                if (!(identicalNext && !sample.done)) {
                    model.learnTd(sample.state, sample.action, sample.reward, sample.next,
                                  sample.done, sample.limit, rate);
                }
                // Only strong Delay successes update the rank head (avoids d=0 collapse).
                if (sample.reward >= 0.5f && sample.action > 0)
                    model.learnRank(sample.state, sample.action, sample.limit, rate);
                float scratch[EbNeuralModel::kFeatureDim];
                std::memcpy(scratch, sample.state, sizeof(scratch));
                EbNeuralModel::writeCandidateDelay(scratch, sample.action);
                float target = 0.5f;
                if (sample.done)
                    target = 0.0f;
                else if (sample.reward >= 0.5f && sample.action > 0)
                    target = 1.0f;
                else if (sample.reward > 0.0f)
                    target = 0.45f;
                model.learnConfidence(scratch, target, rate);
                ++updates;
            }
        }
        m_lastTrainUpdates = updates;
        return updates;
    }

    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    EbNeuralSession() = default;

    Sample const* logicalAt(int index) const {
        if (index < 0 || index >= m_count)
            return nullptr;
        int const slot = (m_next - m_count + index + kCapacity * 2) % kCapacity;
        return &m_samples[static_cast<size_t>(slot)];
    }

    std::vector<Sample> m_samples;
    int m_count = 0;
    int m_next = 0;
    int m_attempts = 0;
    int m_lastTrainUpdates = 0;
    int m_persistedThrough = 0;
    int m_filesSaved = 0;
    bool m_attemptOpen = false;
    std::string m_lastPersistPath;
};

#endif
