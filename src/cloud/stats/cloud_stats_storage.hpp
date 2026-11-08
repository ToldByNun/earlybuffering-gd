#ifndef CLOUD_STATS_STORAGE_HPP
#define CLOUD_STATS_STORAGE_HPP

#include <string>

#include "../cloud_config_stats.hpp"

class CloudStatsStorage {
public:
    static void loadLocalSets(CloudConfigStats const& owner);
    static void saveLocalSets(CloudConfigStats& owner);
    static void unmarkLocal(CloudConfigStats& owner, std::string const& packId, bool downloaded,
                            bool rated);
    static std::string ensureClientId(CloudConfigStats& owner);
    static bool hasDownloaded(CloudConfigStats const& owner, std::string const& packId);
    static bool hasRated(CloudConfigStats const& owner, std::string const& packId);
    static int myStarsFor(CloudConfigStats const& owner, std::string const& packId);
    static void markDownloaded(CloudConfigStats& owner, std::string const& packId);
    static void markRated(CloudConfigStats& owner, std::string const& packId, int stars);

private:
    CloudStatsStorage() = delete;
};

#endif
