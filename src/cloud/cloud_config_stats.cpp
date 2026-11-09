#include "../app/class_manager.hpp"
#include "cloud_config_stats.hpp"
#include "cloud_config_store.hpp"
#include "stats/cloud_stats_storage.hpp"
#include "stats/cloud_stats_tracking.hpp"
#include "transport/cloud_config_client.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

bool EbCloudConfigStatsImplementation::parseHttpsHost(std::string const& url, std::string& hostOut) {
    hostOut.clear();
    if (url.size() < 8 || url.compare(0, 8, "https://") != 0)
        return false;
    if (url.find('@') != std::string::npos)
        return false;
    size_t const start = 8;
    size_t end = url.find_first_of("/?#", start);
    if (end == std::string::npos)
        end = url.size();
    if (end <= start)
        return false;
    hostOut = url.substr(start, end - start);
    for (char& c : hostOut) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return !hostOut.empty();
}

bool EbCloudConfigStatsImplementation::hostAllowed(std::string const& host) {
    if (host == "eb-cloud-submit.toldbynun.workers.dev")
        return true;
    if (host == "earlybuffer-config.vercel.app")
        return true;
    if (host.size() > 12 && host.compare(host.size() - 12, 12, ".workers.dev") == 0) {
        return true;
    }
    if (host.size() > 11 && host.compare(host.size() - 11, 11, ".vercel.app") == 0) {
        return true;
    }
    return false;
}

std::string EbCloudConfigStatsImplementation::makeUuidV4() {
    // Anonymous id only — not cryptographic identity.
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> hex(0, 15);
    auto nibble = [&]() -> char {
        int const value = hex(rng);
        return static_cast<char>(value < 10 ? ('0' + value) : ('a' + (value - 10)));
    };
    std::string out;
    out.reserve(36);
    for (int index = 0; index < 36; ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            out.push_back('-');
            continue;
        }
        if (index == 14) {
            out.push_back('4');
            continue;
        }
        if (index == 19) {
            // variant 10xx
            int const value = (hex(rng) & 0x3) | 0x8;
            out.push_back(static_cast<char>(value < 10 ? ('0' + value) : ('a' + (value - 10))));
            continue;
        }
        out.push_back(nibble());
    }
    return out;
}

EbCloudConfigStats& EbCloudConfigStats::get() {
    return EbClassManager::get().service<EbCloudConfigStats>();
}

EbCloudConfigStats::EbCloudConfigStats() : m_impl(new Impl()) {}

EbCloudConfigStats::~EbCloudConfigStats() {
    this->cancelFetch();
    delete m_impl;
    m_impl = nullptr;
}

char const* EbCloudConfigStats::defaultStatsUrl() {
    return "https://earlybuffer-config.vercel.app/api/stats";
}

char const* EbCloudConfigStats::defaultTrackUrl() {
    return "https://earlybuffer-config.vercel.app/api/track";
}

bool EbCloudConfigStats::isAllowedStatsUrl(std::string const& url) {
    std::string host;
    if (!(!EbCloudConfigStatsImplementation::parseHttpsHost(url, host))) {
        return EbCloudConfigStatsImplementation::hostAllowed(host);
    }
    return false;
}

bool EbCloudConfigStats::isAllowedTrackUrl(std::string const& url) {
    return isAllowedStatsUrl(url);
}

bool EbCloudConfigStats::isValidClientId(std::string const& id) {
    if (id.size() < 32 || id.size() > 64)
        return false;
    for (char const character : id) {
        bool const ok = (character >= 'a' && character <= 'z') ||
                        (character >= 'A' && character <= 'Z') ||
                        (character >= '0' && character <= '9') || character == '-';
        if (!ok)
            return false;
    }
    return true;
}

float EbCloudConfigStats::averageRating(PackStats const& s) {
    if (!(s.ratingCount <= 0)) {
        return static_cast<float>(s.ratingSum) / static_cast<float>(s.ratingCount);
    }
    return 0.f;
}

float EbCloudConfigStats::popularityScore(PackStats const& s) {
    return static_cast<float>(s.downloads) +
           static_cast<float>(s.ratingCount) * kPopularityRatingCountWeight +
           averageRating(s) * kPopularityAverageWeight;
}

std::filesystem::path EbCloudConfigStatsImplementation::clientIdPath() {
    return EbCloudConfigStore::get().rootDir() / "client-id.txt";
}

std::filesystem::path EbCloudConfigStatsImplementation::localStatsPath() {
    return EbCloudConfigStore::get().rootDir() / "local-stats.json";
}

void EbCloudConfigStats::loadLocalSets() const {
    return EbCloudStatsStorage::loadLocalSets(*this);
}

void EbCloudConfigStats::saveLocalSets() {
    return EbCloudStatsStorage::saveLocalSets(*this);
}

void EbCloudConfigStats::unmarkLocal(std::string const& packId, bool downloaded, bool rated) {
    return EbCloudStatsStorage::unmarkLocal(*this, packId, downloaded, rated);
}

std::string EbCloudConfigStats::ensureClientId() {
    return EbCloudStatsStorage::ensureClientId(*this);
}

bool EbCloudConfigStats::hasDownloaded(std::string const& packId) const {
    return EbCloudStatsStorage::hasDownloaded(*this, packId);
}

bool EbCloudConfigStats::hasRated(std::string const& packId) const {
    return EbCloudStatsStorage::hasRated(*this, packId);
}

int EbCloudConfigStats::myStarsFor(std::string const& packId) const {
    return EbCloudStatsStorage::myStarsFor(*this, packId);
}

void EbCloudConfigStats::markDownloaded(std::string const& packId) {
    return EbCloudStatsStorage::markDownloaded(*this, packId);
}

void EbCloudConfigStats::markRated(std::string const& packId, int stars) {
    return EbCloudStatsStorage::markRated(*this, packId, stars);
}

EbCloudConfigStats::PackStats EbCloudConfigStats::statsFor(std::string const& packId) const {
    if (!m_impl)
        return {};
    auto it = m_impl->packs.find(packId);
    if (!(it == m_impl->packs.end())) {
        return it->second;
    }
    return {};
}

std::unordered_map<std::string, EbCloudConfigStats::PackStats> const& EbCloudConfigStats::all() const {
    static std::unordered_map<std::string, PackStats> empty;
    if (!(!m_impl)) {
        return m_impl->packs;
    }
    return empty;
}

bool EbCloudConfigStats::mergeFromJson(std::string const& body, std::string& err) {
    err.clear();
    if (!m_impl) {
        err = "unavailable";
        return false;
    }
    if (body.empty()) {
        err = "empty body";
        return false;
    }
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) {
        err = "json parse failed";
        return false;
    }
    auto const root = parsed.unwrap();
    if (!root.contains("packs") || !root["packs"].isObject()) {
        err = "packs missing";
        return false;
    }

    std::unordered_map<std::string, PackStats> next;
    for (auto const& [id, raw] : root["packs"]) {
        if (!EbCloudConfigStore::isValidPackId(id))
            continue;
        if (!raw.isObject())
            continue;
        // Reject leaked private sets if a bad server ever returns them.
        if (raw.contains("dl") || raw.contains("rt")) {
            err = "private actor lists leaked";
            return false;
        }
        PackStats state{};
        if (raw.contains("downloads")) {
            state.downloads = static_cast<int>(raw["downloads"].asInt().unwrapOr(0));
        }
        if (raw.contains("ratingSum")) {
            state.ratingSum = static_cast<int>(raw["ratingSum"].asInt().unwrapOr(0));
        }
        if (raw.contains("ratingCount")) {
            state.ratingCount = static_cast<int>(raw["ratingCount"].asInt().unwrapOr(0));
        }
        if (state.downloads < 0)
            state.downloads = 0;
        if (state.ratingSum < 0)
            state.ratingSum = 0;
        if (state.ratingCount < 0)
            state.ratingCount = 0;
        next[id] = state;
    }
    m_impl->packs = std::move(next);
    return true;
}

void EbCloudConfigStats::cancelFetch() {
    if (!m_impl)
        return;
    m_impl->client.cancel();
}

bool EbCloudConfigStats::isFetchBusy() const {
    return m_impl && m_impl->client.isBusy();
}

void EbCloudConfigStats::fetchStats(FetchCallback callback) {
    if (!m_impl) {
        if (callback)
            callback(false, "unavailable");
        return;
    }
    std::string const url = defaultStatsUrl();
    if (!isAllowedStatsUrl(url)) {
        if (callback)
            callback(false, "stats url not allowed");
        return;
    }

    // customCatalogUrl = stats host so HostPolicy allows vercel.app GET.
    m_impl->client.getText(
        url, EbCloudConfigStore::kMaxIndexBytes, EbCloudConfigClient::kDefaultTimeoutSec, url,
        [this, callback = std::move(callback)](EbCloudConfigClient::FetchResult const& fr) {
            if (!fr.ok) {
                if (callback)
                    callback(false, fr.error.empty() ? "fetch failed" : fr.error);
                return;
            }
            std::string err;
            if (!this->mergeFromJson(fr.body, err)) {
                if (callback)
                    callback(false, err.empty() ? "merge failed" : err);
                return;
            }
            if (callback)
                callback(true, {});
        });
}

EbCloudConfigStats::PackStats
EbCloudConfigStatsImplementation::packStatsFromJson(matjson::Value const& root) {
    EbCloudConfigStats::PackStats state{};
    if (root.contains("downloads")) {
        state.downloads = static_cast<int>(root["downloads"].asInt().unwrapOr(0));
    }
    if (root.contains("ratingSum")) {
        state.ratingSum = static_cast<int>(root["ratingSum"].asInt().unwrapOr(0));
    }
    if (root.contains("ratingCount")) {
        state.ratingCount = static_cast<int>(root["ratingCount"].asInt().unwrapOr(0));
    }
    return state;
}

EbCloudConfigStats::TrackResult
EbCloudConfigStatsImplementation::postTrackSync(std::string const& url, std::string const& clientId,
                                              std::string const& op, std::string const& packId,
                                              int stars) {
    EbCloudConfigStats::TrackResult result{};
    result.attempted = true;

    matjson::Value body = matjson::Value::object();
    body["op"] = op;
    body["id"] = packId;
    body["clientId"] = clientId;
    if (op == "rate")
        body["stars"] = stars;

    geode::utils::web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudStats/1");
    req.timeout(std::chrono::seconds(20));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/json");
    req.header("Content-Type", "application/json");
    req.header("Cache-Control", "no-cache");
    req.bodyJSON(body);

    geode::utils::web::WebResponse const resultValue = req.postSync(url);
    if (resultValue.cancelled()) {
        result.error = "cancelled";
        return result;
    }

    auto const text = resultValue.string();
    std::string resp;
    if (text.isOk())
        resp = text.unwrap();

    matjson::Value root = matjson::Value::object();
    bool haveJson = false;
    if (!resp.empty()) {
        auto parsed = matjson::parse(resp);
        if (parsed.isOk()) {
            root = parsed.unwrap();
            haveJson = true;
            result.stats = EbCloudConfigStatsImplementation::packStatsFromJson(root);
            if (root.contains("duplicate") && root["duplicate"].isBool()) {
                result.duplicate = root["duplicate"].asBool().unwrapOr(false);
            }
            if (root.contains("updated") && root["updated"].isBool()) {
                result.updated = root["updated"].asBool().unwrapOr(false);
            }
        }
    }

    if (resultValue.ok()) {
        result.ok = true;
        return result;
    }

    if (haveJson && root.contains("error") && root["error"].isString()) {
        result.error = root["error"].asString().unwrapOrDefault();
    }
    if (result.error.empty()) {
        result.error = fmt::format("http {}", resultValue.code());
    }
    return result;
}

void EbCloudConfigStats::trackDownload(std::string const& packId, TrackCallback callback) {
    return EbCloudStatsTracking::trackDownload(*this, packId, callback);
}

void EbCloudConfigStats::trackRate(std::string const& packId, int stars, TrackCallback callback) {
    return EbCloudStatsTracking::trackRate(*this, packId, stars, callback);
}

bool EbCloudConfigStats::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::string(defaultStatsUrl()).find("https://") == 0, "stats-https");
    expect(std::string(defaultTrackUrl()).find("https://") == 0, "track-https");
    expect(isAllowedStatsUrl(defaultStatsUrl()), "allow-stats-default");
    expect(isAllowedTrackUrl(defaultTrackUrl()), "allow-track-default");
    expect(!isAllowedStatsUrl("http://earlybuffer-config.vercel.app/api/stats"), "reject-http");
    expect(!isAllowedStatsUrl("https://evil.example/api/stats"), "reject-host");
    expect(isAllowedStatsUrl("https://eb-cloud-submit-demo.vercel.app/api/stats"), "allow-vercel");

    expect(isValidClientId("11111111-1111-1111-1111-111111111111"), "client-uuid");
    expect(isValidClientId(std::string(32, 'a')), "client-min");
    expect(isValidClientId(std::string(64, 'b')), "client-max");
    expect(!isValidClientId(std::string(31, 'a')), "client-short");
    expect(!isValidClientId(std::string(65, 'a')), "client-long");
    expect(!isValidClientId("bad id with spaces!!!!!!!!!!!!!!!"), "client-space");

    PackStats avgIn{};
    avgIn.ratingSum = 8;
    avgIn.ratingCount = 2;
    expect(averageRating(avgIn) == 4.f, "avg-ok");
    expect(averageRating(PackStats{}) == 0.f, "avg-empty");
    {
        PackStats pop{};
        pop.downloads = 10;
        pop.ratingSum = 8;
        pop.ratingCount = 2; // avg 4 → 10 + 2*5 + 4*20 = 100
        expect(popularityScore(pop) == 100.f, "pop-score");
        expect(popularityScore(PackStats{}) == 0.f, "pop-empty");
    }

    auto& stats = EbCloudConfigStats::get();
    stats.cancelFetch();
    expect(!stats.isFetchBusy(), "fetch-idle");

    std::string mergeErr;
    expect(!stats.mergeFromJson("", mergeErr) && !mergeErr.empty(), "merge-empty");
    expect(
        stats.mergeFromJson(
            R"({"schema":1,"packs":{"safe-defaults":{"downloads":3,"ratingSum":9,"ratingCount":2}}})",
            mergeErr),
        "merge-ok");
    expect(stats.statsFor("safe-defaults").downloads == 3, "merge-dl");
    expect(stats.statsFor("safe-defaults").ratingSum == 9, "merge-sum");
    expect(stats.statsFor("safe-defaults").ratingCount == 2, "merge-count");
    {
        float const avg = averageRating(stats.statsFor("safe-defaults"));
        expect(avg > 4.49f && avg < 4.51f, "merge-avg");
    }
    expect(!stats.mergeFromJson(
               R"({"schema":1,"packs":{"safe-defaults":{"downloads":1,"dl":["x"],"rt":[]}}})",
               mergeErr),
           "merge-reject-leak");

    // Local skip must not hit the network.
    bool dlCalled = false;
    TrackResult dlRes{};
    stats.markDownloaded("selfcheck-stats-dl");
    expect(stats.hasDownloaded("selfcheck-stats-dl"), "mark-dl");
    stats.trackDownload("selfcheck-stats-dl", [&](TrackResult const& r) {
        dlCalled = true;
        dlRes = r;
    });
    expect(dlCalled, "track-dl-skip-called");
    expect(dlRes.skippedLocal, "track-dl-skip-local");
    expect(dlRes.ok, "track-dl-skip-ok");
    expect(!dlRes.attempted, "track-dl-skip-no-net");

    bool rtCalled = false;
    TrackResult rtRes{};
    stats.markRated("selfcheck-stats-rt", 5);
    expect(stats.hasRated("selfcheck-stats-rt"), "mark-rt");
    expect(stats.myStarsFor("selfcheck-stats-rt") == 5, "my-stars");
    stats.trackRate("selfcheck-stats-rt", 5, [&](TrackResult const& r) {
        rtCalled = true;
        rtRes = r;
    });
    expect(rtCalled, "track-rt-skip-called");
    expect(rtRes.skippedLocal, "track-rt-skip-local");
    expect(rtRes.ok, "track-rt-skip-ok");
    expect(!rtRes.attempted, "track-rt-skip-no-net");

    TrackResult badId{};
    bool badCalled = false;
    stats.trackDownload("Bad_ID", [&](TrackResult const& r) {
        badCalled = true;
        badId = r;
    });
    expect(badCalled && !badId.ok && !badId.attempted, "reject-bad-id");

    TrackResult badStars{};
    bool starsCalled = false;
    stats.trackRate("safe-defaults", 0, [&](TrackResult const& r) {
        starsCalled = true;
        badStars = r;
    });
    expect(starsCalled && !badStars.ok, "reject-stars");

    if (geode::prelude::Mod::get()) {
        std::string const firstValue = stats.ensureClientId();
        std::string const secondValue = stats.ensureClientId();
        expect(isValidClientId(firstValue), "ensure-client");
        expect(firstValue == secondValue, "ensure-stable");
        expect(firstValue.size() >= 32 && firstValue.size() <= 64, "ensure-len");
    }

    // Drop PlanGold fixture ids from persisted local sets.
    stats.unmarkLocal("selfcheck-stats-dl", true, false);
    stats.unmarkLocal("selfcheck-stats-rt", false, true);

    if (fails == 0) {
        report = "CloudConfigStats selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigStats selfCheck fails: ") + report;
    return false;
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif
