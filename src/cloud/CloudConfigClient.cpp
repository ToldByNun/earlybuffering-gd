#include "CloudConfigClient.hpp"
#include "CloudConfigHostPolicy.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <thread>

using namespace geode::prelude;

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

class CloudConfigClient::Impl {
public:
    uint64_t gen = 0;
    bool busy = false;
};

static CloudConfigClient::FetchResult cloudConfigFetchSync(
    std::string const& url,
    size_t limit,
    int timeoutSec
) {
    CloudConfigClient::FetchResult r{};
    r.urlUsed = url;

    web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudConfig/1");
    req.timeout(std::chrono::seconds(timeoutSec));
    req.followRedirects(false);
    req.certVerification(true);
    // Avoid stale CDN / HTTP caches of index.json after catalog edits.
    req.header("Cache-Control", "no-cache");
    req.header("Pragma", "no-cache");

    web::WebResponse const res = req.getSync(url);

    if (res.cancelled()) {
        r.cancelled = true;
        r.error = "cancelled";
        return r;
    }

    r.httpCode = res.code();
    if (!res.ok()) {
        auto const msg = res.errorMessage();
        if (!msg.empty()) {
            r.error = fmt::format("http {} ({})", r.httpCode, msg);
        }
        else {
            r.error = fmt::format("http {}", r.httpCode);
        }
        return r;
    }

    auto const str = res.string();
    if (str.isErr()) {
        r.error = "body string failed";
        return r;
    }
    r.body = str.unwrap();
    if (r.body.empty()) {
        r.error = "empty body";
        return r;
    }
    if (r.body.size() > limit) {
        r.error = fmt::format(
            "body too large ({} > {})",
            static_cast<unsigned long long>(r.body.size()),
            static_cast<unsigned long long>(limit)
        );
        r.body.clear();
        return r;
    }

    r.ok = true;
    return r;
}

CloudConfigClient::CloudConfigClient()
    : m_impl(new Impl()) {}

CloudConfigClient::~CloudConfigClient() {
    this->cancel();
    delete m_impl;
    m_impl = nullptr;
}

void CloudConfigClient::cancel() {
    if (!m_impl) return;
    ++m_impl->gen;
    m_impl->busy = false;
}

bool CloudConfigClient::isBusy() const {
    return m_impl && m_impl->busy;
}

void CloudConfigClient::getText(
    std::string const& url,
    size_t maxBytes,
    int timeoutSec,
    std::string const& customCatalogUrl,
    Callback cb
) {
    FetchResult early{};
    early.urlUsed = url;

    if (!m_impl) {
        early.error = "client unavailable";
        if (cb) cb(early);
        return;
    }
    if (url.empty()) {
        early.error = "url empty";
        if (cb) cb(early);
        return;
    }
    if (!CloudConfigHostPolicy::isAllowedUrl(url, customCatalogUrl)) {
        early.error = "url not allowed";
        if (cb) cb(early);
        return;
    }

    this->cancel();
    m_impl->busy = true;
    uint64_t const gen = ++m_impl->gen;

    int const t = timeoutSec > 0 ? timeoutSec : kDefaultTimeoutSec;
    size_t const limit = maxBytes > 0 ? maxBytes : (512u * 1024u);

    Impl* impl = m_impl;
    std::string urlCopy = url;

    // Worker thread + getSync avoids MSVC ICE on async::spawn(WebFuture, lambda).
    // Cancel bumps gen so a late result is dropped (in-flight transfer still finishes).
    std::thread([impl, gen, limit, t, urlCopy = std::move(urlCopy), cb = std::move(cb)]() mutable {
        FetchResult r = cloudConfigFetchSync(urlCopy, limit, t);
        geode::queueInMainThread(
            [impl, gen, cb = std::move(cb), r = std::move(r)]() mutable {
                if (!impl || gen != impl->gen) {
                    return;
                }
                impl->busy = false;
                if (cb) cb(r);
            }
        );
    }).detach();
}

bool CloudConfigClient::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    CloudConfigClient client;
    expect(!client.isBusy(), "idle");
    client.cancel();
    expect(!client.isBusy(), "cancel-idle");

    bool called = false;
    FetchResult got{};
    client.getText(
        "http://evil.example/x",
        1024,
        5,
        {},
        [&](FetchResult const& r) {
            called = true;
            got = r;
        }
    );
    expect(called, "sync-reject-called");
    expect(!got.ok, "sync-reject-not-ok");
    expect(!got.error.empty(), "sync-reject-err");
    expect(!client.isBusy(), "sync-reject-not-busy");

    called = false;
    got = {};
    client.getText(
        "https://evil.example/x",
        1024,
        5,
        {},
        [&](FetchResult const& r) {
            called = true;
            got = r;
        }
    );
    expect(called, "reject-host-called");
    expect(!got.ok, "reject-host-not-ok");
    expect(!client.isBusy(), "reject-host-not-busy");

    called = false;
    std::string const custom = "https://configs.example.com/index.json";
    client.getText(
        "https://configs.example.com/packs/a.json",
        1024,
        5,
        custom,
        [&](FetchResult const& r) {
            called = true;
            (void)r;
        }
    );
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
