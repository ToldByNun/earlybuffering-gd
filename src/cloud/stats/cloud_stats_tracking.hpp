#ifndef CLOUD_STATS_TRACKING_HPP
#define CLOUD_STATS_TRACKING_HPP

#include <string>

#include "../cloud_config_stats.hpp"

class CloudStatsTracking {
public:
    static void trackDownload(CloudConfigStats& owner, std::string const& packId,
                              CloudConfigStats::TrackCallback callback);
    static void trackRate(CloudConfigStats& owner, std::string const& packId, int stars,
                          CloudConfigStats::TrackCallback callback);

private:
    CloudStatsTracking() = delete;
};

#endif
