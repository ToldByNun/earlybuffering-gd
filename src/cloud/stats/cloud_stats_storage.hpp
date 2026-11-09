#ifndef CLOUD_STATS_STORAGE_HPP
#define CLOUD_STATS_STORAGE_HPP

#include <string>

#include "../cloud_config_stats.hpp"

class EbCloudStatsStorage {
public:
    static void loadLocalSets(EbCloudConfigStats const& owner);
    static void saveLocalSets(EbCloudConfigStats& owner);
    static void unmarkLocal(EbCloudConfigStats& owner, std::string const& packId, bool downloaded,
                            bool rated);
    static std::string ensureClientId(EbCloudConfigStats& owner);
    static bool hasDownloaded(EbCloudConfigStats const& owner, std::string const& packId);
    static bool hasRated(EbCloudConfigStats const& owner, std::string const& packId);
    static int myStarsFor(EbCloudConfigStats const& owner, std::string const& packId);
    static void markDownloaded(EbCloudConfigStats& owner, std::string const& packId);
    static void markRated(EbCloudConfigStats& owner, std::string const& packId, int stars);

private:
    EbCloudStatsStorage() = delete;
};

#endif
