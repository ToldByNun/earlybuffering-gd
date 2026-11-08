#ifndef CLOUD_CONFIG_STATS_HPP
#define CLOUD_CONFIG_STATS_HPP

#include <functional>
#include <string>
#include <unordered_map>

// Public catalog counters + anonymous track (1 download + 1 rating per clientId).
// clientId and local downloaded/rated sets live under Mod saveDir/cloud-configs/.
class CloudConfigStats {
public:
    class PackStats {
    public:
        int downloads = 0;
        int ratingSum = 0;
        int ratingCount = 0;

    private:
    };

    class TrackResult {
    public:
        bool attempted = false;
        bool ok = false;
        bool duplicate = false;
        bool updated = false; // re-rate: same voter, new stars (count unchanged)
        bool skippedLocal = false;
        std::string error;
        PackStats stats;

    private:
    };

    using FetchCallback = std::function<void(bool ok, std::string const& error)>;
    using TrackCallback = std::function<void(TrackResult const&)>;

    static CloudConfigStats& get();

    static char const* defaultStatsUrl();
    static char const* defaultTrackUrl();

    static bool isAllowedStatsUrl(std::string const& url);
    static bool isAllowedTrackUrl(std::string const& url);
    // Matches server: [a-zA-Z0-9-]{32,64}
    static bool isValidClientId(std::string const& id);

    // Average stars; 0 when ratingCount == 0.
    static float averageRating(PackStats const& s);

    // Popularity sort key: downloads + ratingCount*k + average*k2.
    static constexpr float kPopularityRatingCountWeight = 5.f;
    static constexpr float kPopularityAverageWeight = 20.f;
    static float popularityScore(PackStats const& s);

    // Load or create UUID in cloud-configs/client-id.txt.
    std::string ensureClientId();

    bool hasDownloaded(std::string const& packId) const;
    bool hasRated(std::string const& packId) const;
    // 0 if this install has not rated the pack yet.
    int myStarsFor(std::string const& packId) const;
    void markDownloaded(std::string const& packId);
    void markRated(std::string const& packId, int stars = 0);

    PackStats statsFor(std::string const& packId) const;
    std::unordered_map<std::string, PackStats> const& all() const;

    // Replace in-memory counters from public /api/stats JSON. No network.
    bool mergeFromJson(std::string const& body, std::string& err);

    // Async GET stats URL. Merges on success. Single-flight.
    void fetchStats(FetchCallback callback);
    void cancelFetch();
    bool isFetchBusy() const;

    // Fire-and-forget POST. Download skips when already in local set.
    // Rate skips only when local myStars == stars (re-rate with a new value is allowed).
    void trackDownload(std::string const& packId, TrackCallback callback = {});
    void trackRate(std::string const& packId, int stars, TrackCallback callback = {});

    // No network — URL allowlist, clientId rules, merge fixture, local skip.
    static bool selfCheck(std::string& report);

private:
    CloudConfigStats();
    ~CloudConfigStats();

    CloudConfigStats(CloudConfigStats const&) = delete;
    CloudConfigStats& operator=(CloudConfigStats const&) = delete;

    void loadLocalSets() const;
    void saveLocalSets();
    void unmarkLocal(std::string const& packId, bool downloaded, bool rated);

    class Impl;
    Impl* m_impl = nullptr;
};

#include "cloud_config_client.hpp"
#include "cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <random>
#include <thread>
#include <unordered_set>

class CloudConfigStatsImplementation {
public:
    static bool parseHttpsHost(std::string const& url, std::string& hostOut);
    static bool hostAllowed(std::string const& host);
    static std::string makeUuidV4();
    static std::filesystem::path clientIdPath();
    static std::filesystem::path localStatsPath();
    static CloudConfigStats::PackStats packStatsFromJson(matjson::Value const& root);
    static CloudConfigStats::TrackResult postTrackSync(std::string const& url,
                                                       std::string const& clientId,
                                                       std::string const& op,
                                                       std::string const& packId, int stars);

private:
    CloudConfigStatsImplementation() = delete;
};

class CloudConfigStats::Impl {
public:
    CloudConfigClient client;
    std::unordered_map<std::string, PackStats> packs;
    // Lazy disk load may run from const getters.
    mutable std::unordered_set<std::string> downloaded;
    mutable std::unordered_set<std::string> rated;
    mutable std::unordered_map<std::string, int> myStars;
    mutable bool localLoaded = false;
    std::string clientId;
    uint64_t trackGen = 0;

private:
};

#endif
