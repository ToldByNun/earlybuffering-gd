#include "SpotTrustMap.hpp"

#include <cmath>
#include <fmt/format.h>

SpotTrustMap& SpotTrustMap::get() {
    static SpotTrustMap inst;
    return inst;
}

int SpotTrustMap::spotBucket(float x) {
    return static_cast<int>(std::floor(x / SpotTrustMap::kSpotSize));
}

void SpotTrustMap::clear() {
    this->m_spots.clear();
}

void SpotTrustMap::markUntrusted(float x) {
    int const center = SpotTrustMap::spotBucket(x);
    for (int d = -SpotTrustMap::kMarkBucketRadius; d <= SpotTrustMap::kMarkBucketRadius; ++d) {
        auto& spot = this->m_spots[center + d];
        ++spot.hits;
        spot.goodStreak = 0;
    }
}

bool SpotTrustMap::isUntrusted(float x) const {
    int const b = SpotTrustMap::spotBucket(x);
    return this->m_spots.find(b) != this->m_spots.end();
}

void SpotTrustMap::noteGoodParity(float x) {
    int const b = SpotTrustMap::spotBucket(x);
    auto it = this->m_spots.find(b);
    if (it == this->m_spots.end()) return;
    ++it->second.goodStreak;
    // Need a few clean probes before trusting the spot again.
    if (it->second.goodStreak >= 3) {
        this->m_spots.erase(it);
    }
}

bool SpotTrustMap::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    SpotTrustMap& map = SpotTrustMap::get();
    map.clear();
    expect(!map.isUntrusted(100.f), "fresh-trusted");
    expect(map.untrustedCount() == 0, "fresh-empty");

    map.markUntrusted(100.f);
    expect(map.isUntrusted(100.f), "marked-center");
    expect(map.isUntrusted(100.f + SpotTrustMap::kSpotSize * 0.5f), "marked-same-bucket");
    // Neighbor bucket within mark radius.
    expect(map.isUntrusted(100.f + SpotTrustMap::kSpotSize), "marked-neighbor");
    expect(map.untrustedCount() >= 1, "count-after-mark");

    map.noteGoodParity(100.f);
    map.noteGoodParity(100.f);
    expect(map.isUntrusted(100.f), "need-three-good");
    map.noteGoodParity(100.f);
    expect(!map.isUntrusted(100.f), "cleared-after-three");

    map.markUntrusted(500.f);
    map.clear();
    expect(!map.isUntrusted(500.f), "clear-all");

    expect(SpotTrustMap::spotBucket(0.f) == 0, "bucket-0");
    expect(SpotTrustMap::spotBucket(89.f) == 0, "bucket-89");
    expect(SpotTrustMap::spotBucket(90.f) == 1, "bucket-90");

    if (fails == 0) {
        report = "SpotTrustMap selfCheck OK";
        return true;
    }
    report = fmt::format("SpotTrustMap selfCheck {} fails: {}", fails, report);
    return false;
}
