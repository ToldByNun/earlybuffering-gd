#include "../../app/class_manager.hpp"
#include "spot_trust_map.hpp"
#include <cmath>
#include <fmt/format.h>

EbSpotTrustMap& EbSpotTrustMap::get() {
    return EbClassManager::get().service<EbSpotTrustMap>();
}

int EbSpotTrustMap::spotBucket(float x) {
    return static_cast<int>(std::floor(x / EbSpotTrustMap::kSpotSize));
}

void EbSpotTrustMap::clear() {
    this->m_spots.clear();
}

void EbSpotTrustMap::markUntrusted(float x) {
    int const center = EbSpotTrustMap::spotBucket(x);
    for (int delay = -EbSpotTrustMap::kMarkBucketRadius; delay <= EbSpotTrustMap::kMarkBucketRadius;
         ++delay) {
        auto& spot = this->m_spots[center + delay];
        ++spot.hits;
        spot.goodStreak = 0;
    }
}

bool EbSpotTrustMap::isUntrusted(float x) const {
    int const secondValue = EbSpotTrustMap::spotBucket(x);
    return this->m_spots.find(secondValue) != this->m_spots.end();
}

void EbSpotTrustMap::noteGoodParity(float x) {
    int const secondValue = EbSpotTrustMap::spotBucket(x);
    auto it = this->m_spots.find(secondValue);
    if (it == this->m_spots.end())
        return;
    ++it->second.goodStreak;
    // Need a few clean probes before trusting the spot again.
    if (it->second.goodStreak >= 3) {
        this->m_spots.erase(it);
    }
}

bool EbSpotTrustMap::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    EbSpotTrustMap& map = EbSpotTrustMap::get();
    map.clear();
    expect(!map.isUntrusted(100.f), "fresh-trusted");
    expect(map.untrustedCount() == 0, "fresh-empty");

    map.markUntrusted(100.f);
    expect(map.isUntrusted(100.f), "marked-center");
    expect(map.isUntrusted(100.f + EbSpotTrustMap::kSpotSize * 0.5f), "marked-same-bucket");
    // Neighbor bucket within mark radius.
    expect(map.isUntrusted(100.f + EbSpotTrustMap::kSpotSize), "marked-neighbor");
    expect(map.untrustedCount() >= 1, "count-after-mark");

    map.noteGoodParity(100.f);
    map.noteGoodParity(100.f);
    expect(map.isUntrusted(100.f), "need-three-good");
    map.noteGoodParity(100.f);
    expect(!map.isUntrusted(100.f), "cleared-after-three");

    map.markUntrusted(500.f);
    map.clear();
    expect(!map.isUntrusted(500.f), "clear-all");

    expect(EbSpotTrustMap::spotBucket(0.f) == 0, "bucket-0");
    expect(EbSpotTrustMap::spotBucket(89.f) == 0, "bucket-89");
    expect(EbSpotTrustMap::spotBucket(90.f) == 1, "bucket-90");

    if (fails == 0) {
        report = "SpotTrustMap selfCheck OK";
        return true;
    }
    report = fmt::format("SpotTrustMap selfCheck {} fails: {}", fails, report);
    return false;
}
