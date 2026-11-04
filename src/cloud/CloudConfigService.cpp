#include "CloudConfigService.hpp"
#include "CloudConfigClient.hpp"
#include "CloudConfigHostPolicy.hpp"
#include "CloudConfigIndex.hpp"
#include "CloudConfigSeed.hpp"
#include "CloudConfigSha256.hpp"
#include "CloudConfigStore.hpp"

#include <Geode/Geode.hpp>

#include <chrono>
#include <utility>

using namespace geode::prelude;

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

    static std::string nowStamp() {
        auto const secs = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        return fmt::format("{}", secs);
    }

    void clearBusy() {
        this->busy = false;
        this->tryFallbackPending = false;
    }

    bool applyCatalogBody(
        std::string const& body,
        std::string const& url,
        CatalogSource src,
        std::string& err
    ) {
        CloudConfigIndex::Catalog parsed;
        if (!CloudConfigIndex::parse(body, parsed, err)) {
            return false;
        }
        this->catalog = std::move(parsed);
        this->catalogUrl = url;
        this->source = src;
        return true;
    }

    void writeMeta(bool ok, std::string const& url, std::string const& error) {
        CloudConfigStore::MetaInfo meta;
        meta.lastRefreshOk = ok;
        meta.lastError = error;
        meta.sourceUrl = url;
        meta.updatedAt = nowStamp();
        std::string ignore;
        (void)CloudConfigStore::get().writeMeta(meta, ignore);
    }
};

CloudConfigService& CloudConfigService::get() {
    static CloudConfigService inst;
    return inst;
}

CloudConfigService::CloudConfigService()
    : m_impl(new Impl()) {}

CloudConfigService::~CloudConfigService() {
    this->cancel();
    delete m_impl;
    m_impl = nullptr;
}

void CloudConfigService::cancel() {
    if (!m_impl) return;
    m_impl->client.cancel();
    m_impl->clearBusy();
}

bool CloudConfigService::isBusy() const {
    return m_impl && m_impl->busy;
}

CloudConfigIndex::Catalog const& CloudConfigService::catalog() const {
    static CloudConfigIndex::Catalog empty;
    if (!m_impl) return empty;
    return m_impl->catalog;
}

std::string const& CloudConfigService::catalogUrl() const {
    static std::string empty;
    if (!m_impl) return empty;
    return m_impl->catalogUrl;
}

CloudConfigService::CatalogSource CloudConfigService::source() const {
    if (!m_impl) return CatalogSource::None;
    return m_impl->source;
}

std::string CloudConfigService::readCatalogUrlSetting() {
    auto* mod = Mod::get();
    if (!mod) return {};
    // Setting is added in a later slice; tolerate missing key.
    if (!mod->hasSetting("cloud-catalog-url")) return {};
    return mod->getSettingValue<std::string>("cloud-catalog-url");
}

bool CloudConfigService::loadLocalCatalog(RefreshResult& out) {
    out = {};
    if (!m_impl) {
        out.error = "service unavailable";
        return false;
    }

    auto& store = CloudConfigStore::get();
    std::string body;
    std::string err;

    if (store.readIndexCache(body, err)) {
        if (m_impl->applyCatalogBody(
                body,
                m_impl->catalogUrl.empty()
                    ? CloudConfigHostPolicy::defaultIndexUrl()
                    : m_impl->catalogUrl,
                CatalogSource::DiskCache,
                err
            )) {
            out.ok = true;
            out.source = CatalogSource::DiskCache;
            out.catalog = m_impl->catalog;
            out.catalogUrl = m_impl->catalogUrl;
            return true;
        }
        // Corrupt cache — fall through to seed.
        body.clear();
        err.clear();
    }

    if (CloudConfigSeed::readIndex(body, err)) {
        std::string applyErr;
        if (m_impl->applyCatalogBody(
                body,
                "seed://cloud-seed/index.json",
                CatalogSource::Seed,
                applyErr
            )) {
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

void CloudConfigService::refreshCatalog(
    std::string const& customCatalogUrl,
    RefreshCallback cb
) {
    RefreshResult early{};
    if (!m_impl) {
        early.error = "service unavailable";
        if (cb) cb(early);
        return;
    }
    if (m_impl->busy) {
        early.busy = true;
        early.error = "busy";
        early.catalog = m_impl->catalog;
        early.catalogUrl = m_impl->catalogUrl;
        early.source = m_impl->source;
        if (cb) cb(early);
        return;
    }

    std::string const primary = CloudConfigHostPolicy::effectiveIndexUrl(customCatalogUrl);
    // Only use built-in GH raw fallback when the caller did not pin a custom URL
    // (effective may still be default if custom was rejected).
    bool const useBuiltInFallback =
        customCatalogUrl.empty()
        || !CloudConfigHostPolicy::isAllowedUrl(customCatalogUrl, customCatalogUrl);

    m_impl->busy = true;
    m_impl->allowCustom = useBuiltInFallback ? std::string{} : customCatalogUrl;
    m_impl->tryFallbackPending = useBuiltInFallback;
    m_impl->lastRefreshError.clear();

    auto finishOk = [this, cb](
        std::string const& body,
        std::string const& url,
        bool usedFallback
    ) {
        if (!m_impl) return;
        std::string err;
        RefreshResult r{};
        r.usedFallback = usedFallback;
        r.catalogUrl = url;
        if (!m_impl->applyCatalogBody(body, url, CatalogSource::Network, err)) {
            m_impl->writeMeta(false, url, err);
            m_impl->clearBusy();
            r.error = err.empty() ? "index parse failed" : err;
            r.catalog = m_impl->catalog;
            r.source = m_impl->source;
            if (cb) cb(r);
            return;
        }
        std::string writeErr;
        (void)CloudConfigStore::get().ensureDirs(writeErr);
        if (!CloudConfigStore::get().writeIndexCache(body, writeErr)) {
            // Keep in-memory catalog even if disk cache write fails.
            m_impl->lastRefreshError = writeErr;
        }
        m_impl->writeMeta(true, url, {});
        m_impl->clearBusy();
        r.ok = true;
        r.source = CatalogSource::Network;
        r.catalog = m_impl->catalog;
        r.catalogUrl = m_impl->catalogUrl;
        if (cb) cb(r);
    };

    auto finishFail = [this, cb](std::string const& url, std::string const& error) {
        if (!m_impl) return;
        m_impl->writeMeta(false, url, error);
        m_impl->clearBusy();
        RefreshResult r{};
        r.error = error;
        r.catalogUrl = url;
        r.catalog = m_impl->catalog;
        r.source = m_impl->source;
        if (cb) cb(r);
    };

    auto startGet = [this, finishOk, finishFail](std::string const& url, bool isFallback) {
        m_impl->client.getText(
            url,
            CloudConfigStore::kMaxIndexBytes,
            CloudConfigClient::kDefaultTimeoutSec,
            m_impl->allowCustom,
            [this, finishOk, finishFail, url, isFallback](
                CloudConfigClient::FetchResult const& fr
            ) {
                if (!m_impl) return;
                if (fr.cancelled) {
                    m_impl->clearBusy();
                    RefreshResult r{};
                    r.error = "cancelled";
                    r.catalog = m_impl->catalog;
                    r.source = m_impl->source;
                    // cancel() bumps client gen; this path is rare.
                    return;
                }
                if (fr.ok) {
                    finishOk(fr.body, url, isFallback);
                    return;
                }

                m_impl->lastRefreshError = fr.error.empty() ? "fetch failed" : fr.error;

                if (!isFallback && m_impl->tryFallbackPending) {
                    m_impl->tryFallbackPending = false;
                    std::string const fb = CloudConfigHostPolicy::fallbackIndexUrl();
                    m_impl->client.getText(
                        fb,
                        CloudConfigStore::kMaxIndexBytes,
                        CloudConfigClient::kDefaultTimeoutSec,
                        m_impl->allowCustom,
                        [finishOk, finishFail, fb](
                            CloudConfigClient::FetchResult const& fr2
                        ) {
                            if (fr2.cancelled) {
                                return;
                            }
                            if (fr2.ok) {
                                finishOk(fr2.body, fb, true);
                                return;
                            }
                            finishFail(
                                fb,
                                fr2.error.empty() ? "fallback fetch failed" : fr2.error
                            );
                        }
                    );
                    return;
                }

                finishFail(url, m_impl->lastRefreshError);
            }
        );
    };

    startGet(primary, false);
}

void CloudConfigService::fetchPack(
    std::string const& id,
    bool forceNetwork,
    PackCallback cb
) {
    PackFetchResult early{};
    early.id = id;

    if (!m_impl) {
        early.error = "service unavailable";
        if (cb) cb(early);
        return;
    }
    if (m_impl->busy) {
        early.busy = true;
        early.error = "busy";
        if (cb) cb(early);
        return;
    }
    if (!CloudConfigStore::isValidPackId(id)) {
        early.error = "invalid pack id";
        if (cb) cb(early);
        return;
    }

    auto const* entry = CloudConfigIndex::findById(m_impl->catalog, id);
    if (!entry) {
        early.error = "pack not in catalog";
        if (cb) cb(early);
        return;
    }

    auto verifyBody = [&](std::string const& body, std::string& err) -> bool {
        if (body.size() != entry->size) {
            err = fmt::format(
                "size mismatch ({} != {})",
                static_cast<unsigned long long>(body.size()),
                static_cast<unsigned long long>(entry->size)
            );
            return false;
        }
        auto const hex = CloudConfigSha256::hashHex(body);
        if (hex != entry->sha256) {
            err = "sha256 mismatch";
            return false;
        }
        return true;
    };

    auto& store = CloudConfigStore::get();

    if (!forceNetwork && store.hasPack(id)) {
        std::string body;
        std::string err;
        if (store.readPack(id, body, err) && verifyBody(body, err)) {
            early.ok = true;
            early.fromCache = true;
            early.body = std::move(body);
            if (cb) cb(early);
            return;
        }
        // Stale/corrupt cache — fall through to network / seed.
    }

    if (!forceNetwork && m_impl->source == CatalogSource::Seed) {
        std::string body;
        std::string err;
        if (CloudConfigSeed::readPack(id, body, err) && verifyBody(body, err)) {
            std::string writeErr;
            (void)store.ensureDirs(writeErr);
            (void)store.writePack(id, body, writeErr);
            early.ok = true;
            early.fromCache = true;
            early.body = std::move(body);
            if (cb) cb(early);
            return;
        }
    }

    std::string packUrl;
    std::string resolveErr;
    std::string const baseUrl = m_impl->catalogUrl.empty()
        ? CloudConfigHostPolicy::defaultIndexUrl()
        : m_impl->catalogUrl;
    // seed:// cannot resolve — try default host if catalog came from seed.
    std::string resolveBase = baseUrl;
    if (resolveBase.rfind("seed://", 0) == 0) {
        resolveBase = CloudConfigHostPolicy::defaultIndexUrl();
    }
    if (!CloudConfigHostPolicy::resolvePackUrl(
            resolveBase, entry->path, packUrl, resolveErr
        )) {
        early.error = resolveErr.empty() ? "resolve failed" : resolveErr;
        if (cb) cb(early);
        return;
    }

    m_impl->busy = true;
    std::string const allowCustom = m_impl->allowCustom.empty()
        ? (resolveBase == CloudConfigHostPolicy::defaultIndexUrl()
               || resolveBase == CloudConfigHostPolicy::fallbackIndexUrl()
               ? std::string{}
               : resolveBase)
        : m_impl->allowCustom;

    m_impl->client.getText(
        packUrl,
        CloudConfigStore::kMaxPackBytes,
        CloudConfigClient::kDefaultTimeoutSec,
        allowCustom,
        [this, cb = std::move(cb), id, entrySha = entry->sha256, entrySize = entry->size](
            CloudConfigClient::FetchResult const& fr
        ) {
            if (!m_impl) return;
            PackFetchResult r{};
            r.id = id;

            auto done = [&]() {
                m_impl->clearBusy();
                if (cb) cb(r);
            };

            if (fr.cancelled) {
                r.cancelled = true;
                r.error = "cancelled";
                done();
                return;
            }
            if (!fr.ok) {
                r.error = fr.error.empty() ? "pack fetch failed" : fr.error;
                done();
                return;
            }
            if (fr.body.size() != entrySize) {
                r.error = fmt::format(
                    "size mismatch ({} != {})",
                    static_cast<unsigned long long>(fr.body.size()),
                    static_cast<unsigned long long>(entrySize)
                );
                done();
                return;
            }
            if (CloudConfigSha256::hashHex(fr.body) != entrySha) {
                r.error = "sha256 mismatch";
                done();
                return;
            }

            std::string writeErr;
            (void)CloudConfigStore::get().ensureDirs(writeErr);
            (void)CloudConfigStore::get().writePack(id, fr.body, writeErr);
            r.ok = true;
            r.body = fr.body;
            done();
        }
    );
}

bool CloudConfigService::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    std::string clientReport;
    expect(CloudConfigClient::selfCheck(clientReport), "client");

    auto& svc = CloudConfigService::get();
    svc.cancel();
    expect(!svc.isBusy(), "idle");

    RefreshResult local{};
    expect(svc.loadLocalCatalog(local), "load-local");
    expect(local.ok, "load-local-ok");
    expect(
        local.source == CatalogSource::DiskCache
            || local.source == CatalogSource::Seed,
        "load-local-source"
    );
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
    svc.fetchPack(
        "not-a-real-pack-id-zz",
        false,
        [&](PackFetchResult const& r) {
            packCalled = true;
            packRes = r;
        }
    );
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
