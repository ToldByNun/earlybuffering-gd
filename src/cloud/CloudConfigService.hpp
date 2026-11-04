#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSERVICE_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSERVICE_HPP

#include "CloudConfigIndex.hpp"

#include <functional>
#include <string>

// Facade: refresh catalog (primary → fallback), cache, seed/offline load, pack download.
class CloudConfigService {
public:
    enum class CatalogSource {
        None,
        Network,
        DiskCache,
        Seed
    };

    struct RefreshResult {
        bool ok = false;
        bool busy = false;
        CatalogSource source = CatalogSource::None;
        CloudConfigIndex::Catalog catalog;
        std::string catalogUrl;
        std::string error;
        bool usedFallback = false;
    };

    struct PackFetchResult {
        bool ok = false;
        bool busy = false;
        bool cancelled = false;
        std::string id;
        std::string body;
        std::string error;
        bool fromCache = false;
    };

    using RefreshCallback = std::function<void(RefreshResult const&)>;
    using PackCallback = std::function<void(PackFetchResult const&)>;

    static CloudConfigService& get();

    // Load disk cache, else bundled seed, into memory. No network.
    bool loadLocalCatalog(RefreshResult& out);

    // Network refresh. Single-flight: second call gets busy=true.
    // customCatalogUrl empty → HostPolicy default (+ built-in fallback on fail).
    // forceRefresh: drop disk index-cache first + cache-bust query on fetch URLs.
    void refreshCatalog(
        std::string const& customCatalogUrl,
        RefreshCallback cb,
        bool forceRefresh = false
    );

    // Download pack if not cached or forceNetwork. Verifies size + sha256.
    void fetchPack(
        std::string const& id,
        bool forceNetwork,
        PackCallback cb
    );

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

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSERVICE_HPP
