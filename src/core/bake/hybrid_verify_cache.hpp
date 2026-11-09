#ifndef HYBRID_VERIFY_CACHE_HPP
#define HYBRID_VERIFY_CACHE_HPP

#include "../../config/stage_config.hpp"
#include "bake_verifier.hpp"

// Attempt-scoped memo for Hybrid BakeVerifier results.
// Key = bake window fields + delay (no bake-vector pointers).
// Cleared on reset / death. X drift invalidates a hit.
class EbHybridVerifyCache {
public:
    class Key {
    public:
        int nominalFrame = 0;
        int earliest = 0;
        int latest = 0;
        int delayTicks = 0;
        int limit = 0;
        bool down = true;
        bool player2 = false;
        GameModeKind mode = GameModeKind::Cube;

    private:
    };

    static EbHybridVerifyCache& get();

    void clearAttempt();

    // True if a matching entry exists and |playerX − stored| ≤ kXTol.
    bool tryGet(Key const& key, float playerX, EbBakeVerifier::Result& out) const;

    // Stores result for this attempt. Skip budgetHit (envelope differs each press).
    void put(Key const& key, float playerX, EbBakeVerifier::Result const& result);

private:
    friend class EbServiceStore;

    EbHybridVerifyCache() = default;

    static constexpr float kXTol = 8.f;
    static constexpr int kMaxEntries = 8;

    static bool keyEqual(Key const& a, Key const& b);

    class Entry {
    public:
        bool valid = false;
        Key key{};
        float playerX = 0.f;
        EbBakeVerifier::Result result{};

    private:
    };

    Entry m_entries[kMaxEntries]{};
    int m_next = 0;
};

#endif
