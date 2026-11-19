#ifndef HYBRID_MODEL_HPP
#define HYBRID_MODEL_HPP
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// Tabular player-driven RL: Q(s,a) ≈ score with visit counts, UCB exploration,
// neighbor soft-updates, and immediate verify outcomes.
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
    // Immediate physics probe feedback (may update within the same attempt).
    void noteProbeOutcome(uint64_t key, int delay, bool verifiedSafe);
    // Soft-boost delays strictly between two strong survivors (e.g. 1 and 4 → 2,3).
    void bridgeBetweenSurvivors(uint64_t key, int limit, int minimumAttempts);
    // Bleed a fraction of the update into delay±1 (local TD generalization).
    void setNeighborBleed(float value);
    void setUcbCoefficient(float value);
    float neighborBleed() const;
    float ucbCoefficient() const;
    int proposal(uint64_t key, int minimumAttempts = 3, int limit = 96) const;
    // UCB-ranked delays (explore) or mature-only scores (propose), then interpolated mids.
    std::vector<int> rankedDelays(uint64_t key, int minimumAttempts, int limit,
                                  bool explore = true) const;
    bool hasEvidence(uint64_t key) const;
    int familiar() const;
    int uniqueKeyCount() const;
    unsigned totalPulls() const;
    void setMaxUniqueKeys(int value);
    void setMaxActionsPerSpot(int value);
    int maxUniqueKeys() const;
    int maxActionsPerSpot() const;
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
    unsigned m_totalPulls = 0;
    bool m_dirty = false;
    bool m_frozen = false;
    mutable int m_familiar = -1;
    int m_maxUniqueKeys = 512;
    int m_maxActionsPerSpot = 4;
    float m_neighborBleed = .35f;
    float m_ucbCoefficient = 1.25f;
    int actionSlots() const;
    int retention(Spot const& entry) const;
    Spot* findWeakestSpot();
    void evictIfOverCap(uint64_t incomingKey);
    void applyReward(Action& action, int reward, bool sameAttempt);
    void softNeighbor(Spot& spot, int delay, int reward, int limit);
    double ucbValue(Action const& action) const;
};

#endif
