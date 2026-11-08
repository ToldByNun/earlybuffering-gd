#ifndef CLOUD_CONFIG_SERVICE_HPP
#define CLOUD_CONFIG_SERVICE_HPP

#include "cloud_config_index.hpp"
#include <functional>
#include <string>

// Facade: refresh catalog (primary → fallback), cache, seed/offline load, pack download.
class CloudConfigService {
public:
    enum class CatalogSource { None, Network, DiskCache, Seed };

    class RefreshResult {
    public:
        bool ok = false;
        bool busy = false;
        CatalogSource source = CatalogSource::None;
        CloudConfigIndex::Catalog catalog;
        std::string catalogUrl;
        std::string error;
        bool usedFallback = false;

    private:
    };

    class PackFetchResult {
    public:
        bool ok = false;
        bool busy = false;
        bool cancelled = false;
        std::string id;
        std::string body;
        std::string error;
        bool fromCache = false;

    private:
    };

    using RefreshCallback = std::function<void(RefreshResult const&)>;
    using PackCallback = std::function<void(PackFetchResult const&)>;

    static CloudConfigService& get();

    // Load disk cache, else bundled seed, into memory. No network.
    bool loadLocalCatalog(RefreshResult& out);

    // Network refresh. Single-flight: second call gets busy=true.
    // customCatalogUrl empty → HostPolicy default (+ built-in fallback on fail).
    // forceRefresh: drop disk index-cache first + cache-bust query on fetch URLs.
    void refreshCatalog(std::string const& customCatalogUrl, RefreshCallback callback,
                        bool forceRefresh = false);

    // Download pack if not cached or forceNetwork. Verifies size + sha256.
    void fetchPack(std::string const& id, bool forceNetwork, PackCallback callback);

    void cancel();
    bool isBusy() const;

    CloudConfigIndex::Catalog const& catalog() const;
    std::string const& catalogUrl() const;
    CatalogSource source() const;

    // Prefer reading Mod setting when present; else empty.
    static std::string readCatalogUrlSetting();

    static bool selfCheck(std::string& report);

private:
    CloudConfigService();
    ~CloudConfigService();

    CloudConfigService(CloudConfigService const&) = delete;
    CloudConfigService& operator=(CloudConfigService const&) = delete;

    class Impl;
    Impl* m_impl = nullptr;
};

#include "cloud_config_client.hpp"
#include "cloud_config_host_policy.hpp"
#include "cloud_config_seed.hpp"
#include "cloud_config_sha256.hpp"
#include "cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <utility>

class CloudConfigServiceImplementation {
public:
    static std::string stripUrlQuery(std::string url);
    static std::string withCacheBust(std::string url);

private:
    CloudConfigServiceImplementation() = delete;
};

class CloudConfigService::Impl {
public:
    CloudConfigClient client;
    CloudConfigIndex::Catalog catalog;
    std::string catalogUrl;
    CatalogSource source = CatalogSource::None;
    bool busy = false;
    // Custom URL for the in-flight refresh (HostPolicy allow-context for packs).
    std::string allowCustom;
    // When true, built-in primary failed → try fallbackIndexUrl once.
    bool tryFallbackPending = false;
    std::string lastRefreshError;

    static std::string nowStamp();

    void clearBusy();

    bool applyCatalogBody(std::string const& body, std::string const& url, CatalogSource source,
                          std::string& err);

    void writeMeta(bool ok, std::string const& url, std::string const& error);

private:
};

#endif
