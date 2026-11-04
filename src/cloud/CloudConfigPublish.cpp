#include "CloudConfigPublish.hpp"
#include "CloudConfigIndex.hpp"
#include "CloudConfigSha256.hpp"
#include "CloudConfigStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

using namespace geode::prelude;

namespace {

constexpr char const* kDefaultRepo = "ToldByNun/earlybuffer-configs";
constexpr char const* kBranch = "main";
constexpr int kTimeoutSec = 25;

bool httpGetJson(
    std::string const& url,
    std::string const& token,
    matjson::Value& out,
    int& code,
    std::string& err
) {
    out = matjson::Value();
    code = 0;
    err.clear();

    web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudPublish/1");
    req.timeout(std::chrono::seconds(kTimeoutSec));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/vnd.github+json");
    req.header("X-GitHub-Api-Version", "2022-11-28");
    req.header("Authorization", std::string("Bearer ") + token);

    web::WebResponse const res = req.getSync(url);
    code = res.code();
    if (res.cancelled()) {
        err = "cancelled";
        return false;
    }
    auto const str = res.string();
    if (str.isErr()) {
        err = fmt::format("http {} (no body)", code);
        return false;
    }
    auto const body = str.unwrap();
    if (code == 404) {
        err = "not found";
        return false;
    }
    if (!res.ok()) {
        err = fmt::format("http {}", code);
        if (!body.empty() && body.size() < 400) err += std::string(" ") + body;
        return false;
    }
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) {
        err = "json parse failed";
        return false;
    }
    out = parsed.unwrap();
    return true;
}

bool httpPutJson(
    std::string const& url,
    std::string const& token,
    matjson::Value const& body,
    matjson::Value& out,
    int& code,
    std::string& err
) {
    out = matjson::Value();
    code = 0;
    err.clear();

    web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudPublish/1");
    req.timeout(std::chrono::seconds(kTimeoutSec));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/vnd.github+json");
    req.header("X-GitHub-Api-Version", "2022-11-28");
    req.header("Authorization", std::string("Bearer ") + token);
    req.bodyJSON(body);

    web::WebResponse const res = req.putSync(url);
    code = res.code();
    if (res.cancelled()) {
        err = "cancelled";
        return false;
    }
    auto const str = res.string();
    std::string respBody;
    if (str.isOk()) respBody = str.unwrap();
    if (!res.ok()) {
        err = fmt::format("http {}", code);
        if (!respBody.empty() && respBody.size() < 500) err += std::string(" ") + respBody;
        return false;
    }
    if (!respBody.empty()) {
        auto parsed = matjson::parse(respBody);
        if (parsed.isOk()) out = parsed.unwrap();
    }
    return true;
}

std::string b64Encode(std::string const& raw) {
    return utils::base64::encode(raw, utils::base64::Base64Variant::Normal);
}

bool b64Decode(std::string const& b64, std::string& out, std::string& err) {
    auto decoded = utils::base64::decodeString(b64, utils::base64::Base64Variant::Normal);
    if (decoded.isErr()) {
        err = "base64 decode failed";
        return false;
    }
    out = decoded.unwrap();
    return true;
}

matjson::Value packEntryObject(
    CloudConfigPublish::PackMeta const& meta,
    std::string const& sha256,
    size_t size
) {
    matjson::Value tags = matjson::Value::array();
    for (auto const& t : meta.tags) tags.push(t);
    if (meta.tags.empty()) tags.push("shared");

    matjson::Value p = matjson::Value::object();
    p["id"] = meta.id;
    p["name"] = meta.name;
    p["author"] = meta.author;
    p["description"] = meta.description;
    p["tags"] = tags;
    p["minModVersion"] = meta.minModVersion.empty() ? "1.4.1" : meta.minModVersion;
    p["bundleVersion"] = meta.bundleVersion <= 0 ? 2 : meta.bundleVersion;
    p["sha256"] = sha256;
    p["path"] = std::string("packs/") + meta.id + ".json";
    p["size"] = static_cast<int64_t>(size);
    return p;
}

} // namespace

bool CloudConfigPublish::isValidRepoSlug(std::string const& slug) {
    // owner/name — alnum, dash, underscore, dot
    auto const slash = slug.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= slug.size()) return false;
    if (slug.find('/', slash + 1) != std::string::npos) return false;
    for (char const c : slug) {
        unsigned char const uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == '/') continue;
        return false;
    }
    return true;
}

std::string CloudConfigPublish::contentsApiUrl(
    std::string const& repo,
    std::string const& path
) {
    return fmt::format("https://api.github.com/repos/{}/contents/{}", repo, path);
}

std::string CloudConfigPublish::isoNowUtc() {
    using clock = std::chrono::system_clock;
    auto const t = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

bool CloudConfigPublish::readEnableSetting() {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting("cloud-publish-enable")) return false;
    return mod->getSettingValue<bool>("cloud-publish-enable");
}

std::string CloudConfigPublish::readTokenSetting() {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting("cloud-publish-token")) return {};
    return mod->getSettingValue<std::string>("cloud-publish-token");
}

std::string CloudConfigPublish::readRepoSetting() {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting("cloud-publish-repo")) return kDefaultRepo;
    auto const v = mod->getSettingValue<std::string>("cloud-publish-repo");
    if (v.empty()) return kDefaultRepo;
    return v;
}

bool CloudConfigPublish::isConfigured() {
    if (!readEnableSetting()) return false;
    auto const token = readTokenSetting();
    if (token.empty()) return false;
    return isValidRepoSlug(readRepoSetting());
}

CloudConfigPublish::Result CloudConfigPublish::publishPack(
    PackMeta const& metaIn,
    std::string const& bundleJson
) {
    Result r{};
    r.attempted = true;
    r.repo = readRepoSetting();

    if (!readEnableSetting()) {
        r.attempted = false;
        r.error = "publish disabled";
        return r;
    }
    std::string const token = readTokenSetting();
    if (token.empty()) {
        r.error = "cloud-publish-token empty";
        return r;
    }
    if (!isValidRepoSlug(r.repo)) {
        r.error = "invalid cloud-publish-repo";
        return r;
    }
    if (!CloudConfigStore::isValidPackId(metaIn.id)) {
        r.error = "invalid pack id";
        return r;
    }
    if (bundleJson.empty() || bundleJson.size() > CloudConfigStore::kMaxPackBytes) {
        r.error = "bundle size invalid";
        return r;
    }
    auto bundleParsed = matjson::parse(bundleJson);
    if (bundleParsed.isErr()) {
        r.error = "bundle json invalid";
        return r;
    }

    PackMeta meta = metaIn;
    std::string const packRel = std::string("packs/") + meta.id + ".json";
    r.packPath = packRel;
    std::string const sha256 = CloudConfigSha256::hashHex(bundleJson);
    size_t const size = bundleJson.size();

    // --- PUT pack file ---
    {
        std::string packSha;
        matjson::Value existing;
        int code = 0;
        std::string err;
        std::string const packUrl = contentsApiUrl(r.repo, packRel);
        if (httpGetJson(packUrl, token, existing, code, err)) {
            if (existing.contains("sha") && existing["sha"].isString()) {
                packSha = existing["sha"].asString().unwrapOrDefault();
            }
        }
        else if (code != 404 && err != "not found") {
            r.error = std::string("pack GET: ") + err;
            return r;
        }

        matjson::Value putBody = matjson::Value::object();
        putBody["message"] = fmt::format("cloud: upsert pack {}", meta.id);
        putBody["content"] = b64Encode(bundleJson);
        putBody["branch"] = kBranch;
        if (!packSha.empty()) {
            putBody["sha"] = packSha;
            r.created = false;
        }
        else {
            r.created = true;
        }

        matjson::Value putRes;
        if (!httpPutJson(packUrl, token, putBody, putRes, code, err)) {
            r.error = std::string("pack PUT: ") + err;
            return r;
        }
        if (putRes.contains("content") && putRes["content"].isObject()) {
            auto const& c = putRes["content"];
            if (c.contains("html_url") && c["html_url"].isString()) {
                r.commitUrl = c["html_url"].asString().unwrapOrDefault();
            }
        }
        if (r.commitUrl.empty() && putRes.contains("commit") && putRes["commit"].isObject()) {
            auto const& c = putRes["commit"];
            if (c.contains("html_url") && c["html_url"].isString()) {
                r.commitUrl = c["html_url"].asString().unwrapOrDefault();
            }
        }
    }

    // --- GET / upsert index.json ---
    {
        std::string indexSha;
        matjson::Value indexRoot = matjson::Value::object();
        indexRoot["schema"] = 1;
        indexRoot["updatedAt"] = isoNowUtc();
        indexRoot["packs"] = matjson::Value::array();

        matjson::Value existing;
        int code = 0;
        std::string err;
        std::string const indexUrl = contentsApiUrl(r.repo, "index.json");
        if (httpGetJson(indexUrl, token, existing, code, err)) {
            if (existing.contains("sha") && existing["sha"].isString()) {
                indexSha = existing["sha"].asString().unwrapOrDefault();
            }
            if (existing.contains("content") && existing["content"].isString()) {
                std::string decoded;
                std::string decErr;
                auto b64 = existing["content"].asString().unwrapOrDefault();
                // GitHub may insert newlines in base64.
                b64.erase(
                    std::remove(b64.begin(), b64.end(), '\n'),
                    b64.end()
                );
                if (b64Decode(b64, decoded, decErr)) {
                    auto parsed = matjson::parse(decoded);
                    if (parsed.isOk()) indexRoot = parsed.unwrap();
                }
            }
        }
        else if (code != 404 && err != "not found") {
            r.error = std::string("index GET: ") + err;
            return r;
        }

        if (!indexRoot.isObject()) indexRoot = matjson::Value::object();
        indexRoot["schema"] = 1;
        indexRoot["updatedAt"] = isoNowUtc();

        matjson::Value packsArr = matjson::Value::array();
        if (indexRoot.contains("packs") && indexRoot["packs"].isArray()) {
            packsArr = indexRoot["packs"];
        }

        matjson::Value newEntry = packEntryObject(meta, sha256, size);
        bool replaced = false;
        matjson::Value next = matjson::Value::array();
        auto const arr = packsArr.asArray().unwrap();
        for (auto const& item : arr) {
            if (item.isObject() && item.contains("id") && item["id"].isString()
                && item["id"].asString().unwrapOrDefault() == meta.id) {
                next.push(newEntry);
                replaced = true;
            }
            else {
                next.push(item);
            }
        }
        if (!replaced) next.push(newEntry);
        indexRoot["packs"] = next;

        // Validate before write.
        {
            CloudConfigIndex::Catalog cat{};
            std::string valErr;
            if (!CloudConfigIndex::parse(indexRoot.dump(), cat, valErr)) {
                r.error = std::string("index validate: ") + valErr;
                return r;
            }
        }

        matjson::Value putBody = matjson::Value::object();
        putBody["message"] = fmt::format("cloud: index upsert {}", meta.id);
        putBody["content"] = b64Encode(indexRoot.dump());
        putBody["branch"] = kBranch;
        if (!indexSha.empty()) putBody["sha"] = indexSha;

        matjson::Value putRes;
        if (!httpPutJson(indexUrl, token, putBody, putRes, code, err)) {
            r.error = std::string("index PUT: ") + err;
            return r;
        }
    }

    r.ok = true;
    if (r.commitUrl.empty()) {
        r.commitUrl = fmt::format(
            "https://github.com/{}/blob/{}/{}",
            r.repo,
            kBranch,
            packRel
        );
    }
    return r;
}

bool CloudConfigPublish::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(isValidRepoSlug("ToldByNun/earlybuffer-configs"), "repo-ok");
    expect(!isValidRepoSlug("nope"), "repo-bad");
    expect(!isValidRepoSlug("a/b/c"), "repo-extra");
    expect(!isoNowUtc().empty(), "iso-now");
    expect(contentsApiUrl("o/r", "index.json").find("api.github.com") != std::string::npos, "api-url");

    // No network in selfCheck — configured() may be false without token.
    expect(!publishPack({}, "{}").ok, "reject-empty-meta");

    if (fails == 0) {
        report = "CloudConfigPublish selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigPublish selfCheck fails: ") + report;
    return false;
}
