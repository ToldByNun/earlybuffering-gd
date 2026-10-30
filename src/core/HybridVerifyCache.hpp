#ifndef EARLYBUFFER_CORE_HYBRIDVERIFYCACHE_HPP
#define EARLYBUFFER_CORE_HYBRIDVERIFYCACHE_HPP

#include "BakeVerifier.hpp"
#include "../config/StageConfig.hpp"

// Attempt-scoped memo for Hybrid BakeVerifier results.
// Key = bake window fields + delay (no bake-vector pointers).
// Cleared on reset / death. X drift invalidates a hit.
class HybridVerifyCache {
public:
    struct Key {
        int nominalFrame = 0;
        int earliest = 0;
        int latest = 0;
        int delayTicks = 0;
        int limit = 0;
        bool down = true;
        bool player2 = false;
        GameModeKind mode = GameModeKind::Cube;
    };

    static HybridVerifyCache& get();

    void clearAttempt();

    // True if a matching entry exists and |playerX − stored| ≤ kXTol.
    bool tryGet(Key const& key, float playerX, BakeVerifier::Result& out) const;

    // Stores result for this attempt. Skip budgetHit (envelope differs each press).
    void put(Key const& key, float playerX, BakeVerifier::Result const& result);

private:
    HybridVerifyCache() = default;

    static constexpr float kXTol = 8.f;
    static constexpr int kMaxEntries = 8;

    static bool keyEqual(Key const& a, Key const& b);

    struct Entry {
        bool valid = false;
        Key key{};
        float playerX = 0.f;
        BakeVerifier::Result result{};
    };

    Entry m_entries[kMaxEntries]{};
    int m_next = 0;
};

#endif // EARLYBUFFER_CORE_HYBRIDVERIFYCACHE_HPP
