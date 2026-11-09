#ifndef CLOUD_STATS_TRACKING_HPP
#define CLOUD_STATS_TRACKING_HPP

#include <string>

#include "../cloud_config_stats.hpp"

class EbCloudStatsTracking {
public:
    static void trackDownload(EbCloudConfigStats& owner, std::string const& packId,
                              EbCloudConfigStats::TrackCallback callback);
    static void trackRate(EbCloudConfigStats& owner, std::string const& packId, int stars,
                          EbCloudConfigStats::TrackCallback callback);

private:
    EbCloudStatsTracking() = delete;
};

#endif
