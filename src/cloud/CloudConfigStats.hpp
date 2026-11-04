#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSTATS_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSTATS_HPP

#include <functional>
#include <string>
#include <unordered_map>

// Public catalog counters + anonymous track (1 download + 1 rating per clientId).
// clientId and local downloaded/rated sets live under Mod saveDir/cloud-configs/.
class CloudConfigStats {
public:
    struct PackStats {
        int downloads = 0;
        int ratingSum = 0;
        int ratingCount = 0;
    };

    struct TrackResult {
        bool attempted = false;
        bool ok = false;
        bool duplicate = false;
        bool skippedLocal = false;
        std::string error;
        PackStats stats;
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

    // Load or create UUID in cloud-configs/client-id.txt.
    std::string ensureClientId();

    bool hasDownloaded(std::string const& packId) const;
    bool hasRated(std::string const& packId) const;
    void markDownloaded(std::string const& packId);
    void markRated(std::string const& packId);

    PackStats statsFor(std::string const& packId) const;
    std::unordered_map<std::string, PackStats> const& all() const;

    // Replace in-memory counters from public /api/stats JSON. No network.
    bool mergeFromJson(std::string const& body, std::string& err);

    // Async GET stats URL. Merges on success. Single-flight.
    void fetchStats(FetchCallback cb);
    void cancelFetch();
    bool isFetchBusy() const;

    // Fire-and-forget POST. Skips network when pack already in local set.
    // Always sends clientId. On success or duplicate/already-rated, marks local.
    void trackDownload(std::string const& packId, TrackCallback cb = {});
    void trackRate(std::string const& packId, int stars, TrackCallback cb = {});

    // No network — URL allowlist, clientId rules, merge fixture, local skip.
    static bool selfCheck(std::string& report);

private:
    CloudConfigStats();
    ~CloudConfigStats();

    CloudConfigStats(CloudConfigStats const&) = delete;
    CloudConfigStats& operator=(CloudConfigStats const&) = delete;

    class Impl;
    Impl* m_impl = nullptr;
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSTATS_HPP
