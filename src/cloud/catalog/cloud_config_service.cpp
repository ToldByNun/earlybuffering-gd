#include "../../app/class_manager.hpp"
#include "cloud_config_service.hpp"
#include "../cloud_config_store.hpp"
#include "../transport/cloud_config_client.hpp"
#include "../transport/cloud_config_host_policy.hpp"
#include "../transport/cloud_config_sha256.hpp"
#include "cloud_config_index.hpp"
#include "cloud_config_seed.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <utility>

EbCloudConfigService& EbCloudConfigService::get() {
    return EbClassManager::get().service<EbCloudConfigService>();
}

EbCloudConfigService::EbCloudConfigService() : m_impl(new Impl()) {}

EbCloudConfigService::~EbCloudConfigService() {
    this->cancel();
    delete m_impl;
    m_impl = nullptr;
}

void EbCloudConfigService::cancel() {
    if (!m_impl)
        return;
    m_impl->client.cancel();
    m_impl->clearBusy();
}

bool EbCloudConfigService::isBusy() const {
    return m_impl && m_impl->busy;
}

EbCloudConfigIndex::Catalog const& EbCloudConfigService::catalog() const {
    static EbCloudConfigIndex::Catalog empty;
    if (!(!m_impl)) {
        return m_impl->catalog;
    }
    return empty;
}

std::string const& EbCloudConfigService::catalogUrl() const {
    static std::string empty;
    if (!(!m_impl)) {
        return m_impl->catalogUrl;
    }
    return empty;
}

EbCloudConfigService::CatalogSource EbCloudConfigService::source() const {
    if (!(!m_impl)) {
        return m_impl->source;
    }
    return CatalogSource::None;
}

std::string EbCloudConfigService::readCatalogUrlSetting() {
    // Built-in catalog only (ToldByNun/earlybuffer-configs via HostPolicy).
    return {};
}

bool EbCloudConfigService::loadLocalCatalog(RefreshResult& out) {
    out = {};
    if (!m_impl) {
        out.error = "service unavailable";
        return false;
    }

    auto& store = EbCloudConfigStore::get();
    std::string body;
    std::string err;

    if (store.readIndexCache(body, err)) {
        if (m_impl->applyCatalogBody(body,
                                     m_impl->catalogUrl.empty()
                                         ? EbCloudConfigHostPolicy::defaultIndexUrl()
                                         : m_impl->catalogUrl,
                                     CatalogSource::DiskCache, err) &&
            !m_impl->catalog.packs.empty()) {
            out.ok = true;
            out.source = CatalogSource::DiskCache;
            out.catalog = m_impl->catalog;
            out.catalogUrl = m_impl->catalogUrl;
            return true;
        }
        // Corrupt / empty cache — fall through to seed.
        body.clear();
        err.clear();
    }

    if (EbCloudConfigSeed::readIndex(body, err)) {
        std::string applyErr;
        if (m_impl->applyCatalogBody(body, "seed://cloud-seed/index.json", CatalogSource::Seed,
                                     applyErr)) {
            out.ok = true;
            out.source = CatalogSource::Seed;
            out.catalog = m_impl->catalog;
            out.catalogUrl = m_impl->catalogUrl;
            return true;
        }
        out.error = applyErr.empty() ? "seed parse failed" : applyErr;
        return false;
    }

    out.error = err.empty() ? "no local catalog" : err;
    return false;
}

std::string EbCloudConfigServiceImplementation::stripUrlQuery(std::string url) {
    auto const queryOffset = url.find_first_of("?#");
    if (queryOffset != std::string::npos)
        url.resize(queryOffset);
    return url;
}

std::string EbCloudConfigServiceImplementation::withCacheBust(std::string url) {
    url = EbCloudConfigServiceImplementation::stripUrlQuery(std::move(url));
    auto const elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
    return url + "?eb=" + std::to_string(elapsedMilliseconds);
}

void EbCloudConfigService::refreshCatalog(std::string const& customCatalogUrl,
                                        RefreshCallback callback, bool forceRefresh) {
    RefreshResult early{};
    if (!m_impl) {
        early.error = "service unavailable";
        if (callback)
            callback(early);
        return;
    }
    if (m_impl->busy) {
        early.busy = true;
        early.error = "busy";
        early.catalog = m_impl->catalog;
        early.catalogUrl = m_impl->catalogUrl;
        early.source = m_impl->source;
        if (callback)
            callback(early);
        return;
    }

    std::string const primary = EbCloudConfigHostPolicy::effectiveIndexUrl(customCatalogUrl);
    // Only use built-in GH raw fallback when the caller did not pin a custom URL
    // (effective may still be default if custom was rejected).
    bool const useBuiltInFallback =
        customCatalogUrl.empty() ||
        !EbCloudConfigHostPolicy::isAllowedUrl(customCatalogUrl, customCatalogUrl);

    if (forceRefresh) {
        std::string clearErr;
        (void)EbCloudConfigStore::get().clearIndexCache(clearErr);
    }

    m_impl->busy = true;
    m_impl->allowCustom = useBuiltInFallback ? std::string{} : customCatalogUrl;
    m_impl->tryFallbackPending = useBuiltInFallback;
    m_impl->lastRefreshError.clear();

    auto finishOk = [this, callback](std::string const& body, std::string const& url,
                                     bool usedFallback) {
        if (!m_impl)
            return;
        // Pack resolve must use a clean catalog URL (no ?eb= cache buster).
        std::string const cleanUrl = EbCloudConfigServiceImplementation::stripUrlQuery(url);
        std::string err;
        RefreshResult result{};
        result.usedFallback = usedFallback;
        result.catalogUrl = cleanUrl;
        if (!m_impl->applyCatalogBody(body, cleanUrl, CatalogSource::Network, err)) {
            m_impl->writeMeta(false, cleanUrl, err);
            m_impl->clearBusy();
            result.error = err.empty() ? "index parse failed" : err;
            result.catalog = m_impl->catalog;
            result.source = m_impl->source;
            if (callback)
                callback(result);
            return;
        }
        std::string writeErr;
        (void)EbCloudConfigStore::get().ensureDirs(writeErr);
        if (!EbCloudConfigStore::get().writeIndexCache(body, writeErr)) {
            // Keep in-memory catalog even if disk cache write fails.
            m_impl->lastRefreshError = writeErr;
        }
        m_impl->writeMeta(true, cleanUrl, {});
        m_impl->clearBusy();
        result.ok = true;
        result.source = CatalogSource::Network;
        result.catalog = m_impl->catalog;
        result.catalogUrl = m_impl->catalogUrl;
        if (callback)
            callback(result);
    };

    auto finishFail = [this, callback](std::string const& url, std::string const& error) {
        if (!m_impl)
            return;
        std::string const cleanUrl = EbCloudConfigServiceImplementation::stripUrlQuery(url);
        m_impl->writeMeta(false, cleanUrl, error);
        m_impl->clearBusy();
        RefreshResult result{};
        result.error = error;
        result.catalogUrl = cleanUrl;
        result.catalog = m_impl->catalog;
        result.source = m_impl->source;
        if (callback)
            callback(result);
    };

    auto startGet = [this, finishOk, finishFail](std::string const& url, bool isFallback) {
        std::string const fetchUrl = EbCloudConfigServiceImplementation::withCacheBust(url);
        m_impl->client.getText(
            fetchUrl, EbCloudConfigStore::kMaxIndexBytes, EbCloudConfigClient::kDefaultTimeoutSec,
            m_impl->allowCustom,
            [this, finishOk, finishFail, url,
             isFallback](EbCloudConfigClient::FetchResult const& fr) {
                if (!m_impl)
                    return;
                if (fr.cancelled) {
                    m_impl->clearBusy();
                    RefreshResult result{};
                    result.error = "cancelled";
                    result.catalog = m_impl->catalog;
                    result.source = m_impl->source;
                    // cancel() bumps client gen; this path is rare.
                    return;
                }
                if (fr.ok) {
                    // Prefer clean base URL for storage / pack resolve.
                    finishOk(fr.body, url, isFallback);
                    return;
                }

                m_impl->lastRefreshError = fr.error.empty() ? "fetch failed" : fr.error;

                if (!isFallback && m_impl->tryFallbackPending) {
                    m_impl->tryFallbackPending = false;
                    std::string const fb = EbCloudConfigHostPolicy::fallbackIndexUrl();
                    std::string const fbFetch = EbCloudConfigServiceImplementation::withCacheBust(fb);
                    m_impl->client.getText(
                        fbFetch, EbCloudConfigStore::kMaxIndexBytes,
                        EbCloudConfigClient::kDefaultTimeoutSec, m_impl->allowCustom,
                        [finishOk, finishFail, fb](EbCloudConfigClient::FetchResult const& fr2) {
                            if (fr2.cancelled) {
                                return;
                            }
                            if (fr2.ok) {
                                finishOk(fr2.body, fb, true);
                                return;
                            }
                            finishFail(fb, fr2.error.empty() ? "fallback fetch failed" : fr2.error);
                        });
                    return;
                }

                finishFail(url, m_impl->lastRefreshError);
            });
    };

    startGet(primary, false);
}

void EbCloudConfigService::fetchPack(std::string const& id, bool forceNetwork,
                                   PackCallback callback) {
    PackFetchResult early{};
    early.id = id;

    if (!m_impl) {
        early.error = "service unavailable";
        if (callback)
            callback(early);
        return;
    }
    if (m_impl->busy) {
        early.busy = true;
        early.error = "busy";
        if (callback)
            callback(early);
        return;
    }
    if (!EbCloudConfigStore::isValidPackId(id)) {
        early.error = "invalid pack id";
        if (callback)
            callback(early);
        return;
    }

    auto const* entry = EbCloudConfigIndex::findById(m_impl->catalog, id);
    if (!entry) {
        early.error = "pack not in catalog";
        if (callback)
            callback(early);
        return;
    }

    auto verifyBody = [&](std::string const& body, std::string& err) -> bool {
        if (body.size() != entry->size) {
            err = fmt::format("size mismatch ({} != {})",
                              static_cast<unsigned long long>(body.size()),
                              static_cast<unsigned long long>(entry->size));
            return false;
        }
        auto const hex = EbCloudConfigSha256::hashHex(body);
        if (hex != entry->sha256) {
            err = "sha256 mismatch";
            return false;
        }
        return true;
    };

    auto& store = EbCloudConfigStore::get();

    if (!forceNetwork && store.hasPack(id)) {
        std::string body;
        std::string err;
        if (store.readPack(id, body, err) && verifyBody(body, err)) {
            early.ok = true;
            early.fromCache = true;
            early.body = std::move(body);
            if (callback)
                callback(early);
            return;
        }
        // Stale/corrupt cache — fall through to network / seed.
    }

    if (!forceNetwork && m_impl->source == CatalogSource::Seed) {
        std::string body;
        std::string err;
        if (EbCloudConfigSeed::readPack(id, body, err) && verifyBody(body, err)) {
            std::string writeErr;
            (void)store.ensureDirs(writeErr);
            (void)store.writePack(id, body, writeErr);
            early.ok = true;
            early.fromCache = true;
            early.body = std::move(body);
            if (callback)
                callback(early);
            return;
        }
    }

    std::string packUrl;
    std::string resolveErr;
    std::string const baseUrl =
        m_impl->catalogUrl.empty() ? EbCloudConfigHostPolicy::defaultIndexUrl() : m_impl->catalogUrl;
    // seed:// cannot resolve — try default host if catalog came from seed.
    std::string resolveBase = baseUrl;
    if (resolveBase.rfind("seed://", 0) == 0) {
        resolveBase = EbCloudConfigHostPolicy::defaultIndexUrl();
    }
    if (!EbCloudConfigHostPolicy::resolvePackUrl(resolveBase, entry->path, packUrl, resolveErr)) {
        early.error = resolveErr.empty() ? "resolve failed" : resolveErr;
        if (callback)
            callback(early);
        return;
    }

    m_impl->busy = true;
    std::string const allowCustom =
        m_impl->allowCustom.empty()
            ? (resolveBase == EbCloudConfigHostPolicy::defaultIndexUrl() ||
                       resolveBase == EbCloudConfigHostPolicy::fallbackIndexUrl()
                   ? std::string{}
                   : resolveBase)
            : m_impl->allowCustom;

    m_impl->client.getText(packUrl, EbCloudConfigStore::kMaxPackBytes,
                           EbCloudConfigClient::kDefaultTimeoutSec, allowCustom,
                           [this, callback = std::move(callback), id, entrySha = entry->sha256,
                            entrySize = entry->size](EbCloudConfigClient::FetchResult const& fr) {
                               if (!m_impl)
                                   return;
                               PackFetchResult result{};
                               result.id = id;

                               auto done = [&]() {
                                   m_impl->clearBusy();
                                   if (callback)
                                       callback(result);
                               };

                               if (fr.cancelled) {
                                   result.cancelled = true;
                                   result.error = "cancelled";
                                   done();
                                   return;
                               }
                               if (!fr.ok) {
                                   result.error = fr.error.empty() ? "pack fetch failed" : fr.error;
                                   done();
                                   return;
                               }
                               if (fr.body.size() != entrySize) {
                                   result.error =
                                       fmt::format("size mismatch ({} != {})",
                                                   static_cast<unsigned long long>(fr.body.size()),
                                                   static_cast<unsigned long long>(entrySize));
                                   done();
                                   return;
                               }
                               if (EbCloudConfigSha256::hashHex(fr.body) != entrySha) {
                                   result.error = "sha256 mismatch";
                                   done();
                                   return;
                               }

                               std::string writeErr;
                               (void)EbCloudConfigStore::get().ensureDirs(writeErr);
                               (void)EbCloudConfigStore::get().writePack(id, fr.body, writeErr);
                               result.ok = true;
                               result.body = fr.body;
                               done();
                           });
}

bool EbCloudConfigService::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    std::string clientReport;
    expect(EbCloudConfigClient::selfCheck(clientReport), "client");

    auto& svc = EbCloudConfigService::get();
    svc.cancel();
    expect(!svc.isBusy(), "idle");

    RefreshResult local{};
    expect(svc.loadLocalCatalog(local), "load-local");
    expect(local.ok, "load-local-ok");
    expect(local.source == CatalogSource::DiskCache || local.source == CatalogSource::Seed,
           "load-local-source");
    expect(!local.catalog.packs.empty(), "load-local-packs");
    expect(svc.source() == local.source, "source-mem");
    expect(!svc.catalog().packs.empty(), "catalog-mem");

    // No network in PlanGold. Single-flight is enforced by the busy flag in
    // refreshCatalog/fetchPack; Client selfCheck covers cancel/reject.
    expect(!svc.isBusy(), "idle-after-local");
    svc.cancel();
    expect(!svc.isBusy(), "cancel-idle");

    bool packCalled = false;
    PackFetchResult packRes{};
    svc.fetchPack("not-a-real-pack-id-zz", false, [&](PackFetchResult const& r) {
        packCalled = true;
        packRes = r;
    });
    expect(packCalled, "pack-missing-called");
    expect(!packRes.ok, "pack-missing-not-ok");
    expect(!svc.isBusy(), "pack-missing-not-busy");

    if (fails == 0) {
        report = "CloudConfigService selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigService selfCheck fails: ") + report;
    return false;
}

auto EbCloudConfigService::Impl::nowStamp() -> std::string {
    auto const secs = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    return fmt::format("{}", secs);
}

auto EbCloudConfigService::Impl::clearBusy() -> void {
    this->busy = false;
    this->tryFallbackPending = false;
}

auto EbCloudConfigService::Impl::applyCatalogBody(std::string const& body, std::string const& url,
                                                CatalogSource sourceValue,
                                                std::string& err) -> bool {
    EbCloudConfigIndex::Catalog parsed;
    if (!EbCloudConfigIndex::parse(body, parsed, err)) {
        return false;
    }
    this->catalog = std::move(parsed);
    this->catalogUrl = url;
    this->source = sourceValue;
    return true;
}

auto EbCloudConfigService::Impl::writeMeta(bool ok, std::string const& url,
                                         std::string const& error) -> void {
    EbCloudConfigStore::MetaInfo meta;
    meta.lastRefreshOk = ok;
    meta.lastError = error;
    meta.sourceUrl = url;
    meta.updatedAt = nowStamp();
    std::string ignore;
    (void)EbCloudConfigStore::get().writeMeta(meta, ignore);
}
