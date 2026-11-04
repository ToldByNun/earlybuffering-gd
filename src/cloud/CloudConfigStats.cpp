#include "CloudConfigStats.hpp"
#include "CloudConfigClient.hpp"
#include "CloudConfigStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <cctype>
#include <filesystem>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

namespace {

bool parseHttpsHost(std::string const& url, std::string& hostOut) {
    hostOut.clear();
    if (url.size() < 8 || url.compare(0, 8, "https://") != 0) return false;
    if (url.find('@') != std::string::npos) return false;
    size_t const start = 8;
    size_t end = url.find_first_of("/?#", start);
    if (end == std::string::npos) end = url.size();
    if (end <= start) return false;
    hostOut = url.substr(start, end - start);
    for (char& c : hostOut) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return !hostOut.empty();
}

bool hostAllowed(std::string const& host) {
    if (host == "eb-cloud-submit.toldbynun.workers.dev") return true;
    if (host == "earlybuffer-config.vercel.app") return true;
    if (host.size() > 12 && host.compare(host.size() - 12, 12, ".workers.dev") == 0) {
        return true;
    }
    if (host.size() > 11 && host.compare(host.size() - 11, 11, ".vercel.app") == 0) {
        return true;
    }
    return false;
}

std::string makeUuidV4() {
    // Anonymous id only — not cryptographic identity.
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> hex(0, 15);
    auto nibble = [&]() -> char {
        int const v = hex(rng);
        return static_cast<char>(v < 10 ? ('0' + v) : ('a' + (v - 10)));
    };
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            out.push_back('-');
            continue;
        }
        if (i == 14) {
            out.push_back('4');
            continue;
        }
        if (i == 19) {
            // variant 10xx
            int const v = (hex(rng) & 0x3) | 0x8;
            out.push_back(static_cast<char>(v < 10 ? ('0' + v) : ('a' + (v - 10))));
            continue;
        }
        out.push_back(nibble());
    }
    return out;
}

} // namespace

class CloudConfigStats::Impl {
public:
    CloudConfigClient client;
    std::unordered_map<std::string, PackStats> packs;
    std::unordered_set<std::string> downloaded;
    std::unordered_set<std::string> rated;
    std::string clientId;
    bool localLoaded = false;
    uint64_t trackGen = 0;
};

CloudConfigStats& CloudConfigStats::get() {
    static CloudConfigStats inst;
    return inst;
}

CloudConfigStats::CloudConfigStats()
    : m_impl(new Impl()) {}

CloudConfigStats::~CloudConfigStats() {
    this->cancelFetch();
    delete m_impl;
    m_impl = nullptr;
}

char const* CloudConfigStats::defaultStatsUrl() {
    return "https://earlybuffer-config.vercel.app/api/stats";
}

char const* CloudConfigStats::defaultTrackUrl() {
    return "https://earlybuffer-config.vercel.app/api/track";
}

bool CloudConfigStats::isAllowedStatsUrl(std::string const& url) {
    std::string host;
    if (!parseHttpsHost(url, host)) return false;
    return hostAllowed(host);
}

bool CloudConfigStats::isAllowedTrackUrl(std::string const& url) {
    return isAllowedStatsUrl(url);
}

bool CloudConfigStats::isValidClientId(std::string const& id) {
    if (id.size() < 32 || id.size() > 64) return false;
    for (char const c : id) {
        bool const ok = (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '-';
        if (!ok) return false;
    }
    return true;
}

float CloudConfigStats::averageRating(PackStats const& s) {
    if (s.ratingCount <= 0) return 0.f;
    return static_cast<float>(s.ratingSum) / static_cast<float>(s.ratingCount);
}

namespace {

std::filesystem::path clientIdPath() {
    return CloudConfigStore::get().rootDir() / "client-id.txt";
}

std::filesystem::path localStatsPath() {
    return CloudConfigStore::get().rootDir() / "local-stats.json";
}

void loadLocalSets(CloudConfigStats::Impl* impl) {
    if (!impl || impl->localLoaded) return;
    impl->localLoaded = true;
    impl->downloaded.clear();
    impl->rated.clear();

    std::string err;
    std::string body;
    if (!CloudConfigStore::readTextFile(localStatsPath(), body, 256u * 1024u, err)) {
        return;
    }
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) return;
    auto const root = parsed.unwrap();
    if (root.contains("downloaded") && root["downloaded"].isArray()) {
        for (auto const& v : root["downloaded"].asArray().unwrap()) {
            if (!v.isString()) continue;
            auto const id = v.asString().unwrapOrDefault();
            if (CloudConfigStore::isValidPackId(id)) impl->downloaded.insert(id);
        }
    }
    if (root.contains("rated") && root["rated"].isArray()) {
        for (auto const& v : root["rated"].asArray().unwrap()) {
            if (!v.isString()) continue;
            auto const id = v.asString().unwrapOrDefault();
            if (CloudConfigStore::isValidPackId(id)) impl->rated.insert(id);
        }
    }
}

void saveLocalSets(CloudConfigStats::Impl* impl) {
    if (!impl) return;
    std::string dirErr;
    if (!CloudConfigStore::get().ensureDirs(dirErr)) return;

    matjson::Value downloaded = matjson::Value::array();
    for (auto const& id : impl->downloaded) downloaded.push(id);
    matjson::Value rated = matjson::Value::array();
    for (auto const& id : impl->rated) rated.push(id);

    matjson::Value root = matjson::Value::object();
    root["downloaded"] = downloaded;
    root["rated"] = rated;

    std::string err;
    (void)CloudConfigStore::writeTextFile(localStatsPath(), root.dump(), err);
}

// Keep local-stats.json tidy after PlanGold fixtures.
void unmarkLocal(
    CloudConfigStats::Impl* impl,
    std::string const& packId,
    bool downloaded,
    bool rated
) {
    if (!impl || !CloudConfigStore::isValidPackId(packId)) return;
    loadLocalSets(impl);
    bool changed = false;
    if (downloaded) changed = impl->downloaded.erase(packId) > 0 || changed;
    if (rated) changed = impl->rated.erase(packId) > 0 || changed;
    if (changed) saveLocalSets(impl);
}

} // namespace

std::string CloudConfigStats::ensureClientId() {
    if (!m_impl) return {};
    if (!m_impl->clientId.empty() && isValidClientId(m_impl->clientId)) {
        return m_impl->clientId;
    }

    std::string dirErr;
    (void)CloudConfigStore::get().ensureDirs(dirErr);

    std::string err;
    std::string existing;
    if (CloudConfigStore::readTextFile(clientIdPath(), existing, 128, err)) {
        // Trim whitespace / newlines.
        while (!existing.empty()
            && (existing.back() == '\n' || existing.back() == '\r'
                || existing.back() == ' ' || existing.back() == '\t')) {
            existing.pop_back();
        }
        size_t start = 0;
        while (start < existing.size()
            && (existing[start] == ' ' || existing[start] == '\t'
                || existing[start] == '\r' || existing[start] == '\n')) {
            ++start;
        }
        if (start > 0) existing = existing.substr(start);
        if (isValidClientId(existing)) {
            m_impl->clientId = existing;
            return m_impl->clientId;
        }
    }

    std::string const fresh = makeUuidV4();
    if (!isValidClientId(fresh)) return {};
    std::string writeErr;
    if (!CloudConfigStore::writeTextFile(clientIdPath(), fresh + "\n", writeErr)) {
        // Still use in-memory id for this session.
        m_impl->clientId = fresh;
        return m_impl->clientId;
    }
    m_impl->clientId = fresh;
    return m_impl->clientId;
}

bool CloudConfigStats::hasDownloaded(std::string const& packId) const {
    if (!m_impl || !CloudConfigStore::isValidPackId(packId)) return false;
    loadLocalSets(m_impl);
    return m_impl->downloaded.count(packId) > 0;
}

bool CloudConfigStats::hasRated(std::string const& packId) const {
    if (!m_impl || !CloudConfigStore::isValidPackId(packId)) return false;
    loadLocalSets(m_impl);
    return m_impl->rated.count(packId) > 0;
}

void CloudConfigStats::markDownloaded(std::string const& packId) {
    if (!m_impl || !CloudConfigStore::isValidPackId(packId)) return;
    loadLocalSets(m_impl);
    if (m_impl->downloaded.insert(packId).second) {
        saveLocalSets(m_impl);
    }
}

void CloudConfigStats::markRated(std::string const& packId) {
    if (!m_impl || !CloudConfigStore::isValidPackId(packId)) return;
    loadLocalSets(m_impl);
    if (m_impl->rated.insert(packId).second) {
        saveLocalSets(m_impl);
    }
}

CloudConfigStats::PackStats CloudConfigStats::statsFor(std::string const& packId) const {
    if (!m_impl) return {};
    auto it = m_impl->packs.find(packId);
    if (it == m_impl->packs.end()) return {};
    return it->second;
}

std::unordered_map<std::string, CloudConfigStats::PackStats> const& CloudConfigStats::all() const {
    static std::unordered_map<std::string, PackStats> empty;
    if (!m_impl) return empty;
    return m_impl->packs;
}

bool CloudConfigStats::mergeFromJson(std::string const& body, std::string& err) {
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
        if (!CloudConfigStore::isValidPackId(id)) continue;
        if (!raw.isObject()) continue;
        // Reject leaked private sets if a bad server ever returns them.
        if (raw.contains("dl") || raw.contains("rt")) {
            err = "private actor lists leaked";
            return false;
        }
        PackStats s{};
        if (raw.contains("downloads")) {
            s.downloads = static_cast<int>(raw["downloads"].asInt().unwrapOr(0));
        }
        if (raw.contains("ratingSum")) {
            s.ratingSum = static_cast<int>(raw["ratingSum"].asInt().unwrapOr(0));
        }
        if (raw.contains("ratingCount")) {
            s.ratingCount = static_cast<int>(raw["ratingCount"].asInt().unwrapOr(0));
        }
        if (s.downloads < 0) s.downloads = 0;
        if (s.ratingSum < 0) s.ratingSum = 0;
        if (s.ratingCount < 0) s.ratingCount = 0;
        next[id] = s;
    }
    m_impl->packs = std::move(next);
    return true;
}

void CloudConfigStats::cancelFetch() {
    if (!m_impl) return;
    m_impl->client.cancel();
}

bool CloudConfigStats::isFetchBusy() const {
    return m_impl && m_impl->client.isBusy();
}

void CloudConfigStats::fetchStats(FetchCallback cb) {
    if (!m_impl) {
        if (cb) cb(false, "unavailable");
        return;
    }
    std::string const url = defaultStatsUrl();
    if (!isAllowedStatsUrl(url)) {
        if (cb) cb(false, "stats url not allowed");
        return;
    }

    // customCatalogUrl = stats host so HostPolicy allows vercel.app GET.
    m_impl->client.getText(
        url,
        CloudConfigStore::kMaxIndexBytes,
        CloudConfigClient::kDefaultTimeoutSec,
        url,
        [this, cb = std::move(cb)](CloudConfigClient::FetchResult const& fr) {
            if (!fr.ok) {
                if (cb) cb(false, fr.error.empty() ? "fetch failed" : fr.error);
                return;
            }
            std::string err;
            if (!this->mergeFromJson(fr.body, err)) {
                if (cb) cb(false, err.empty() ? "merge failed" : err);
                return;
            }
            if (cb) cb(true, {});
        }
    );
}

namespace {

CloudConfigStats::PackStats packStatsFromJson(matjson::Value const& root) {
    CloudConfigStats::PackStats s{};
    if (root.contains("downloads")) {
        s.downloads = static_cast<int>(root["downloads"].asInt().unwrapOr(0));
    }
    if (root.contains("ratingSum")) {
        s.ratingSum = static_cast<int>(root["ratingSum"].asInt().unwrapOr(0));
    }
    if (root.contains("ratingCount")) {
        s.ratingCount = static_cast<int>(root["ratingCount"].asInt().unwrapOr(0));
    }
    return s;
}

CloudConfigStats::TrackResult postTrackSync(
    std::string const& url,
    std::string const& clientId,
    std::string const& op,
    std::string const& packId,
    int stars
) {
    CloudConfigStats::TrackResult r{};
    r.attempted = true;

    matjson::Value body = matjson::Value::object();
    body["op"] = op;
    body["id"] = packId;
    body["clientId"] = clientId;
    if (op == "rate") body["stars"] = stars;

    web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudStats/1");
    req.timeout(std::chrono::seconds(20));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/json");
    req.header("Content-Type", "application/json");
    req.header("Cache-Control", "no-cache");
    req.bodyJSON(body);

    web::WebResponse const res = req.postSync(url);
    if (res.cancelled()) {
        r.error = "cancelled";
        return r;
    }

    auto const str = res.string();
    std::string resp;
    if (str.isOk()) resp = str.unwrap();

    matjson::Value root = matjson::Value::object();
    bool haveJson = false;
    if (!resp.empty()) {
        auto parsed = matjson::parse(resp);
        if (parsed.isOk()) {
            root = parsed.unwrap();
            haveJson = true;
            r.stats = packStatsFromJson(root);
            if (root.contains("duplicate") && root["duplicate"].isBool()) {
                r.duplicate = root["duplicate"].asBool().unwrapOr(false);
            }
        }
    }

    if (res.ok()) {
        r.ok = true;
        return r;
    }

    // Rate already counted — treat as settled for local lock.
    if (res.code() == 409
        || (haveJson && root.contains("error") && root["error"].isString()
            && root["error"].asString().unwrapOrDefault().find("already rated")
                != std::string::npos)) {
        r.ok = false;
        r.duplicate = true;
        r.error = haveJson && root.contains("error") && root["error"].isString()
            ? root["error"].asString().unwrapOrDefault()
            : "already rated";
        return r;
    }

    if (haveJson && root.contains("error") && root["error"].isString()) {
        r.error = root["error"].asString().unwrapOrDefault();
    }
    if (r.error.empty()) {
        r.error = fmt::format("http {}", res.code());
    }
    return r;
}

} // namespace

void CloudConfigStats::trackDownload(std::string const& packId, TrackCallback cb) {
    TrackResult early{};
    if (!m_impl) {
        early.error = "unavailable";
        if (cb) cb(early);
        return;
    }
    if (!CloudConfigStore::isValidPackId(packId)) {
        early.error = "invalid pack id";
        if (cb) cb(early);
        return;
    }
    loadLocalSets(m_impl);
    if (m_impl->downloaded.count(packId) > 0) {
        early.skippedLocal = true;
        early.ok = true;
        early.stats = this->statsFor(packId);
        if (cb) cb(early);
        return;
    }

    std::string const url = defaultTrackUrl();
    if (!isAllowedTrackUrl(url)) {
        early.attempted = true;
        early.error = "track url not allowed";
        if (cb) cb(early);
        return;
    }
    std::string const clientId = this->ensureClientId();
    if (!isValidClientId(clientId)) {
        early.attempted = true;
        early.error = "invalid clientId";
        if (cb) cb(early);
        return;
    }

    Impl* impl = m_impl;
    ++m_impl->trackGen;
    std::string idCopy = packId;

    std::thread([this, impl, url, clientId, idCopy = std::move(idCopy), cb = std::move(cb)]() mutable {
        TrackResult r = postTrackSync(url, clientId, "download", idCopy, 0);
        geode::queueInMainThread(
            [this, impl, idCopy = std::move(idCopy), cb = std::move(cb), r = std::move(r)]() mutable {
                if (!impl) return;
                if (r.ok || r.duplicate) {
                    this->markDownloaded(idCopy);
                    if (r.stats.downloads > 0 || r.stats.ratingCount > 0) {
                        impl->packs[idCopy] = r.stats;
                    }
                }
                if (cb) cb(r);
            }
        );
    }).detach();
}

void CloudConfigStats::trackRate(std::string const& packId, int stars, TrackCallback cb) {
    TrackResult early{};
    if (!m_impl) {
        early.error = "unavailable";
        if (cb) cb(early);
        return;
    }
    if (!CloudConfigStore::isValidPackId(packId)) {
        early.error = "invalid pack id";
        if (cb) cb(early);
        return;
    }
    if (stars < 1 || stars > 5) {
        early.error = "stars must be 1-5";
        if (cb) cb(early);
        return;
    }
    loadLocalSets(m_impl);
    if (m_impl->rated.count(packId) > 0) {
        early.skippedLocal = true;
        early.ok = true;
        early.stats = this->statsFor(packId);
        if (cb) cb(early);
        return;
    }

    std::string const url = defaultTrackUrl();
    if (!isAllowedTrackUrl(url)) {
        early.attempted = true;
        early.error = "track url not allowed";
        if (cb) cb(early);
        return;
    }
    std::string const clientId = this->ensureClientId();
    if (!isValidClientId(clientId)) {
        early.attempted = true;
        early.error = "invalid clientId";
        if (cb) cb(early);
        return;
    }

    Impl* impl = m_impl;
    ++m_impl->trackGen;
    std::string idCopy = packId;

    std::thread([this, impl, url, clientId, idCopy = std::move(idCopy), stars, cb = std::move(cb)]() mutable {
        TrackResult r = postTrackSync(url, clientId, "rate", idCopy, stars);
        geode::queueInMainThread(
            [this, impl, idCopy = std::move(idCopy), cb = std::move(cb), r = std::move(r)]() mutable {
                if (!impl) return;
                if (r.ok || r.duplicate) {
                    this->markRated(idCopy);
                    if (r.stats.downloads > 0 || r.stats.ratingCount > 0 || r.stats.ratingSum > 0) {
                        impl->packs[idCopy] = r.stats;
                    }
                }
                if (cb) cb(r);
            }
        );
    }).detach();
}

bool CloudConfigStats::selfCheck(std::string& report) {
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
    expect(
        isAllowedStatsUrl("https://eb-cloud-submit-demo.vercel.app/api/stats"),
        "allow-vercel"
    );

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

    auto& stats = CloudConfigStats::get();
    stats.cancelFetch();
    expect(!stats.isFetchBusy(), "fetch-idle");

    std::string mergeErr;
    expect(
        !stats.mergeFromJson("", mergeErr) && !mergeErr.empty(),
        "merge-empty"
    );
    expect(
        stats.mergeFromJson(
            R"({"schema":1,"packs":{"safe-defaults":{"downloads":3,"ratingSum":9,"ratingCount":2}}})",
            mergeErr
        ),
        "merge-ok"
    );
    expect(stats.statsFor("safe-defaults").downloads == 3, "merge-dl");
    expect(stats.statsFor("safe-defaults").ratingSum == 9, "merge-sum");
    expect(stats.statsFor("safe-defaults").ratingCount == 2, "merge-count");
    {
        float const avg = averageRating(stats.statsFor("safe-defaults"));
        expect(avg > 4.49f && avg < 4.51f, "merge-avg");
    }
    expect(
        !stats.mergeFromJson(
            R"({"schema":1,"packs":{"safe-defaults":{"downloads":1,"dl":["x"],"rt":[]}}})",
            mergeErr
        ),
        "merge-reject-leak"
    );

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
    stats.markRated("selfcheck-stats-rt");
    expect(stats.hasRated("selfcheck-stats-rt"), "mark-rt");
    stats.trackRate("selfcheck-stats-rt", 5, [&](TrackResult const& r) {
        rtCalled = true;
        rtRes = r;
    });
    expect(rtCalled, "track-rt-skip-called");
    expect(rtRes.skippedLocal, "track-rt-skip-local");
    expect(rtRes.ok, "track-rt-skip-ok");

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

    if (Mod::get()) {
        std::string const a = stats.ensureClientId();
        std::string const b = stats.ensureClientId();
        expect(isValidClientId(a), "ensure-client");
        expect(a == b, "ensure-stable");
        expect(a.size() >= 32 && a.size() <= 64, "ensure-len");
    }

    // Drop PlanGold fixture ids from persisted local sets.
    unmarkLocal(stats.m_impl, "selfcheck-stats-dl", true, false);
    unmarkLocal(stats.m_impl, "selfcheck-stats-rt", false, true);

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
