#ifndef HYBRID_MODEL_HPP
#define HYBRID_MODEL_HPP
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

class EbHybridModel {
public:
    class Evidence {
    public:
        int successCount = 0, failureCount = 0, unknownCount = 0;
        int timingPhase = -1, inputSequenceSignature = -1;
        int deathDeltaTicks = -1, interveningInputs = 0;
        int verifiedRescueCount = 0, verifiedHarmCount = 0;

    private:
    };
    class Inspection {
    public:
        uint64_t key = 0;
        int delay = 0;
        int samples = 0;
        int weight = 0;
        bool selected = false;
        Evidence evidence;

    private:
    };
    std::vector<Inspection> inspect(int minimumAttempts) const;
    static uint64_t mix(uint64_t key, int value);
    void nextAttempt();
    void observe(uint64_t key, int delay, int reward, Evidence const* observation = nullptr);
    int proposal(uint64_t key, int minimumAttempts = 3, int limit = 96) const;
    bool hasEvidence(uint64_t key) const;
    int familiar() const;
    bool save(std::filesystem::path const& path);
    bool load(std::filesystem::path const& path);
    void reset();
    void setFrozen(bool frozen);
    bool frozen() const;

private:
    class Action {
    public:
        int delay = -1;
        int count = 0;
        int score = 0;
        Evidence evidence;

    private:
    };
    class Spot {
    public:
        uint64_t key = 0;
        unsigned attempt = 0;
        std::array<Action, 4> actions{};

    private:
    };
    // Four ways per set bound lookup, allocation and eviction cost per click.
    std::array<std::array<Spot, 4>, 256> m_sets{};
    unsigned m_attempt = 1;
    bool m_dirty = false;
    bool m_frozen = false;
    mutable int m_familiar = -1;
};
#endif
