#ifndef EARLYBUFFER_CORE_TRAVELFORESIGHTCACHE_HPP
#define EARLYBUFFER_CORE_TRAVELFORESIGHTCACHE_HPP

#include <string>

class PlayLayer;
class PlayerObject;

// Same-tick TravelForesight cone result (press gate + spatial hint + HUS).
// Equal answer, fewer full objectLayer scans — does not change cone math.
class TravelForesightCache {
public:
    static constexpr int kXBucketPx = 4;

    static TravelForesightCache& get();

    void clear();

    // Cached hasOrbOrPadAhead for (attemptTick, x bucket, limit, slack, speed).
    bool hasOrbOrPadAhead(
        PlayLayer* layer,
        PlayerObject* player,
        int limit,
        float simDt,
        int slack,
        int attemptTick
    );

    // Test helpers / fixtures.
    int hits() const { return this->m_hits; }
    int misses() const { return this->m_misses; }

    static bool selfCheck(std::string& report);

private:
    TravelForesightCache() = default;

    static int xBucket(float x);
    static int speedQuantum(float speedMul);

    bool m_valid = false;
    int m_tick = -1;
    int m_xBucket = 0;
    int m_limit = -1;
    int m_slack = -1;
    int m_speedQ = 0;
    bool m_hit = false;

    int m_hits = 0;
    int m_misses = 0;
};

#endif // EARLYBUFFER_CORE_TRAVELFORESIGHTCACHE_HPP
