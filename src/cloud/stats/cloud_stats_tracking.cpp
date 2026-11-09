#include "cloud_stats_tracking.hpp"
#include "../cloud_config_stats.hpp"
#include "../cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

void EbCloudStatsTracking::trackDownload(EbCloudConfigStats& owner, std::string const& packId,
                                       EbCloudConfigStats::TrackCallback callback) {
    EbCloudConfigStats::TrackResult early{};
    if (!owner.m_impl) {
        early.error = "unavailable";
        if (callback)
            callback(early);
        return;
    }
    if (!EbCloudConfigStore::isValidPackId(packId)) {
        early.error = "invalid pack id";
        if (callback)
            callback(early);
        return;
    }
    owner.loadLocalSets();
    if (owner.m_impl->downloaded.count(packId) > 0) {
        early.skippedLocal = true;
        early.ok = true;
        early.stats = owner.statsFor(packId);
        if (callback)
            callback(early);
        return;
    }

    std::string const url = owner.defaultTrackUrl();
    if (!owner.isAllowedTrackUrl(url)) {
        early.attempted = true;
        early.error = "track url not allowed";
        if (callback)
            callback(early);
        return;
    }
    std::string const clientId = owner.ensureClientId();
    if (!owner.isValidClientId(clientId)) {
        early.attempted = true;
        early.error = "invalid clientId";
        if (callback)
            callback(early);
        return;
    }

    EbCloudConfigStats::Impl* impl = owner.m_impl;
    ++owner.m_impl->trackGen;
    std::string idCopy = packId;

    std::thread([&owner, impl, url, clientId, idCopy = std::move(idCopy),
                 callback = std::move(callback)]() mutable {
        EbCloudConfigStats::TrackResult result =
            EbCloudConfigStatsImplementation::postTrackSync(url, clientId, "download", idCopy, 0);
        geode::queueInMainThread([&owner, impl, idCopy = std::move(idCopy),
                                  callback = std::move(callback),
                                  result = std::move(result)]() mutable {
            if (!impl)
                return;
            if (result.ok || result.duplicate) {
                owner.markDownloaded(idCopy);
                if (result.stats.downloads > 0 || result.stats.ratingCount > 0) {
                    impl->packs[idCopy] = result.stats;
                }
            }
            if (callback)
                callback(result);
        });
    }).detach();
}

void EbCloudStatsTracking::trackRate(EbCloudConfigStats& owner, std::string const& packId, int stars,
                                   EbCloudConfigStats::TrackCallback callback) {
    EbCloudConfigStats::TrackResult early{};
    if (!owner.m_impl) {
        early.error = "unavailable";
        if (callback)
            callback(early);
        return;
    }
    if (!EbCloudConfigStore::isValidPackId(packId)) {
        early.error = "invalid pack id";
        if (callback)
            callback(early);
        return;
    }
    if (stars < 1 || stars > 5) {
        early.error = "stars must be 1-5";
        if (callback)
            callback(early);
        return;
    }
    owner.loadLocalSets();
    // Same vote again → no network. Different stars → re-rate (server keeps count).
    if (owner.myStarsFor(packId) == stars) {
        early.skippedLocal = true;
        early.ok = true;
        early.duplicate = true;
        early.stats = owner.statsFor(packId);
        if (callback)
            callback(early);
        return;
    }

    std::string const url = owner.defaultTrackUrl();
    if (!owner.isAllowedTrackUrl(url)) {
        early.attempted = true;
        early.error = "track url not allowed";
        if (callback)
            callback(early);
        return;
    }
    std::string const clientId = owner.ensureClientId();
    if (!owner.isValidClientId(clientId)) {
        early.attempted = true;
        early.error = "invalid clientId";
        if (callback)
            callback(early);
        return;
    }

    EbCloudConfigStats::Impl* impl = owner.m_impl;
    ++owner.m_impl->trackGen;
    std::string idCopy = packId;

    std::thread([&owner, impl, url, clientId, idCopy = std::move(idCopy), stars,
                 callback = std::move(callback)]() mutable {
        EbCloudConfigStats::TrackResult result =
            EbCloudConfigStatsImplementation::postTrackSync(url, clientId, "rate", idCopy, stars);
        geode::queueInMainThread([&owner, impl, idCopy = std::move(idCopy), stars,
                                  callback = std::move(callback),
                                  result = std::move(result)]() mutable {
            if (!impl)
                return;
            if (result.ok || result.duplicate) {
                owner.markRated(idCopy, stars);
                if (result.stats.downloads > 0 || result.stats.ratingCount > 0 ||
                    result.stats.ratingSum > 0) {
                    impl->packs[idCopy] = result.stats;
                }
            }
            if (callback)
                callback(result);
        });
    }).detach();
}
