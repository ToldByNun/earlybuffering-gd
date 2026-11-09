#include "cloud_config_client.hpp"
#include "cloud_config_host_policy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>
#include <thread>

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

EbCloudConfigClient::FetchResult
EbCloudConfigClientImplementation::cloudConfigFetchSync(std::string const& url, size_t limit,
                                                      int timeoutSec) {
    EbCloudConfigClient::FetchResult result{};
    result.urlUsed = url;

    geode::utils::web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudConfig/1");
    req.timeout(std::chrono::seconds(timeoutSec));
    req.followRedirects(false);
    req.certVerification(true);
    // Avoid stale CDN / HTTP caches of index.json after catalog edits.
    req.header("Cache-Control", "no-cache");
    req.header("Pragma", "no-cache");

    geode::utils::web::WebResponse const resultValue = req.getSync(url);

    if (resultValue.cancelled()) {
        result.cancelled = true;
        result.error = "cancelled";
        return result;
    }

    result.httpCode = resultValue.code();
    if (!resultValue.ok()) {
        auto const message = resultValue.errorMessage();
        if (!message.empty()) {
            result.error = fmt::format("http {} ({})", result.httpCode, message);
        } else {
            result.error = fmt::format("http {}", result.httpCode);
        }
        return result;
    }

    auto const text = resultValue.string();
    if (text.isErr()) {
        result.error = "body string failed";
        return result;
    }
    result.body = text.unwrap();
    if (result.body.empty()) {
        result.error = "empty body";
        return result;
    }
    if (result.body.size() > limit) {
        result.error = fmt::format("body too large ({} > {})",
                                   static_cast<unsigned long long>(result.body.size()),
                                   static_cast<unsigned long long>(limit));
        result.body.clear();
        return result;
    }

    result.ok = true;
    return result;
}

EbCloudConfigClient::EbCloudConfigClient() : m_impl(new Impl()) {}

EbCloudConfigClient::~EbCloudConfigClient() {
    this->cancel();
    delete m_impl;
    m_impl = nullptr;
}

void EbCloudConfigClient::cancel() {
    if (!m_impl)
        return;
    ++m_impl->gen;
    m_impl->busy = false;
}

bool EbCloudConfigClient::isBusy() const {
    return m_impl && m_impl->busy;
}

void EbCloudConfigClient::getText(std::string const& url, size_t maxBytes, int timeoutSec,
                                std::string const& customCatalogUrl, Callback callback) {
    FetchResult early{};
    early.urlUsed = url;

    if (!m_impl) {
        early.error = "client unavailable";
        if (callback)
            callback(early);
        return;
    }
    if (url.empty()) {
        early.error = "url empty";
        if (callback)
            callback(early);
        return;
    }
    if (!EbCloudConfigHostPolicy::isAllowedUrl(url, customCatalogUrl)) {
        early.error = "url not allowed";
        if (callback)
            callback(early);
        return;
    }

    this->cancel();
    m_impl->busy = true;
    uint64_t const generation = ++m_impl->gen;

    int const tick = timeoutSec > 0 ? timeoutSec : kDefaultTimeoutSec;
    size_t const limit = maxBytes > 0 ? maxBytes : (512u * 1024u);

    Impl* impl = m_impl;
    std::string urlCopy = url;

    // Worker thread + getSync avoids MSVC ICE on async::spawn(WebFuture, lambda).
    // Cancel bumps gen so a late result is dropped (in-flight transfer still finishes).
    std::thread([impl, generation, limit, tick, urlCopy = std::move(urlCopy),
                 callback = std::move(callback)]() mutable {
        FetchResult result =
            EbCloudConfigClientImplementation::cloudConfigFetchSync(urlCopy, limit, tick);
        geode::queueInMainThread([impl, generation, callback = std::move(callback),
                                  result = std::move(result)]() mutable {
            if (!impl || generation != impl->gen) {
                return;
            }
            impl->busy = false;
            if (callback)
                callback(result);
        });
    }).detach();
}

bool EbCloudConfigClient::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    EbCloudConfigClient client;
    expect(!client.isBusy(), "idle");
    client.cancel();
    expect(!client.isBusy(), "cancel-idle");

    bool called = false;
    FetchResult got{};
    client.getText("http://evil.example/x", 1024, 5, {}, [&](FetchResult const& r) {
        called = true;
        got = r;
    });
    expect(called, "sync-reject-called");
    expect(!got.ok, "sync-reject-not-ok");
    expect(!got.error.empty(), "sync-reject-err");
    expect(!client.isBusy(), "sync-reject-not-busy");

    called = false;
    got = {};
    client.getText("https://evil.example/x", 1024, 5, {}, [&](FetchResult const& r) {
        called = true;
        got = r;
    });
    expect(called, "reject-host-called");
    expect(!got.ok, "reject-host-not-ok");
    expect(!client.isBusy(), "reject-host-not-busy");

    called = false;
    std::string const custom = "https://configs.example.com/index.json";
    client.getText("https://configs.example.com/packs/a.json", 1024, 5, custom,
                   [&](FetchResult const& r) {
                       called = true;
                       (void)r;
                   });
    expect(client.isBusy() || called, "custom-allowed-start");
    client.cancel();
    expect(!client.isBusy(), "custom-cancel");

    if (fails == 0) {
        report = "CloudConfigClient selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigClient selfCheck fails: ") + report;
    return false;
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif
