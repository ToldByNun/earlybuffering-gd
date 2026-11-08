#include "hybrid_verify_cache.hpp"
#include <cmath>

HybridVerifyCache& HybridVerifyCache::get() {
    static HybridVerifyCache inst;
    return inst;
}

bool HybridVerifyCache::keyEqual(Key const& a, Key const& b) {
    return a.nominalFrame == b.nominalFrame && a.earliest == b.earliest && a.latest == b.latest &&
           a.delayTicks == b.delayTicks && a.limit == b.limit && a.down == b.down &&
           a.player2 == b.player2 && a.mode == b.mode;
}

void HybridVerifyCache::clearAttempt() {
    for (int index = 0; index < kMaxEntries; ++index) {
        this->m_entries[index] = {};
    }
    this->m_next = 0;
}

bool HybridVerifyCache::tryGet(Key const& key, float playerX, BakeVerifier::Result& out) const {
    for (int index = 0; index < kMaxEntries; ++index) {
        auto const& e = this->m_entries[index];
        if (!e.valid)
            continue;
        if (!keyEqual(e.key, key))
            continue;
        if (std::fabs(playerX - e.playerX) > kXTol)
            continue;
        out = e.result;
        return true;
    }
    return false;
}

void HybridVerifyCache::put(Key const& key, float playerX, BakeVerifier::Result const& result) {
    // Budget exhaustion is press-envelope-local — do not memoize as bake truth.
    if (result.budgetHit)
        return;

    // Update existing key in place if present.
    for (int index = 0; index < kMaxEntries; ++index) {
        auto& e = this->m_entries[index];
        if (!e.valid)
            continue;
        if (!keyEqual(e.key, key))
            continue;
        e.playerX = playerX;
        e.result = result;
        return;
    }

    Entry& slot = this->m_entries[this->m_next];
    this->m_next = (this->m_next + 1) % kMaxEntries;
    slot.valid = true;
    slot.key = key;
    slot.playerX = playerX;
    slot.result = result;
}
