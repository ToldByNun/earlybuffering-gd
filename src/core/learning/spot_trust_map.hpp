#ifndef SPOT_TRUST_MAP_HPP
#define SPOT_TRUST_MAP_HPP

#include <string>
#include <unordered_map>

// Spots where GhostParityProbe saw drift stay untrusted across attempts
// until cleared (good parity window or level quit / reset learn).
class EbSpotTrustMap {
public:
    static constexpr float kSpotSize = 90.f;
    // Mark covers neighboring buckets so a mover corridor stays cold.
    static constexpr int kMarkBucketRadius = 1;

    static EbSpotTrustMap& get();

    void clear();
    void markUntrusted(float x);
    bool isUntrusted(float x) const;
    // After a clean parity probe near x — drop that bucket (not neighbors).
    void noteGoodParity(float x);
    int untrustedCount() const {
        return static_cast<int>(this->m_spots.size());
    }

    static int spotBucket(float x);
    static bool selfCheck(std::string& report);

private:
    friend class EbServiceStore;

    EbSpotTrustMap() = default;

    class Spot {
    public:
        int hits = 0;
        int goodStreak = 0;

    private:
    };
    std::unordered_map<int, Spot> m_spots;
};

#endif
