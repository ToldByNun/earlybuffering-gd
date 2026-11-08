#include "cloud_config_publish.hpp"
#include "../catalog/cloud_config_index.hpp"
#include "../cloud_config_store.hpp"
#include "../transport/cloud_config_sha256.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

bool CloudConfigPublishImplementation::httpGetJson(std::string const& url, std::string const& token,
                                                   matjson::Value& out, int& code,
                                                   std::string& err) {
    out = matjson::Value();
    code = 0;
    err.clear();

    geode::utils::web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudPublish/1");
    req.timeout(std::chrono::seconds(CloudConfigPublishImplementation::kTimeoutSec));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/vnd.github+json");
    req.header("X-GitHub-Api-Version", "2022-11-28");
    req.header("Authorization", std::string("Bearer ") + token);

    geode::utils::web::WebResponse const result = req.getSync(url);
    code = result.code();
    if (result.cancelled()) {
        err = "cancelled";
        return false;
    }
    auto const text = result.string();
    if (text.isErr()) {
        err = fmt::format("http {} (no body)", code);
        return false;
    }
    auto const body = text.unwrap();
    if (code == 404) {
        err = "not found";
        return false;
    }
    if (!result.ok()) {
        err = fmt::format("http {}", code);
        if (!body.empty() && body.size() < 400)
            err += std::string(" ") + body;
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

bool CloudConfigPublishImplementation::httpPutJson(std::string const& url, std::string const& token,
                                                   matjson::Value const& body, matjson::Value& out,
                                                   int& code, std::string& err) {
    out = matjson::Value();
    code = 0;
    err.clear();

    geode::utils::web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudPublish/1");
    req.timeout(std::chrono::seconds(CloudConfigPublishImplementation::kTimeoutSec));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/vnd.github+json");
    req.header("X-GitHub-Api-Version", "2022-11-28");
    req.header("Authorization", std::string("Bearer ") + token);
    req.bodyJSON(body);

    geode::utils::web::WebResponse const result = req.putSync(url);
    code = result.code();
    if (result.cancelled()) {
        err = "cancelled";
        return false;
    }
    auto const text = result.string();
    std::string respBody;
    if (text.isOk())
        respBody = text.unwrap();
    if (!result.ok()) {
        err = fmt::format("http {}", code);
        if (!respBody.empty() && respBody.size() < 500)
            err += std::string(" ") + respBody;
        return false;
    }
    if (!respBody.empty()) {
        auto parsed = matjson::parse(respBody);
        if (parsed.isOk())
            out = parsed.unwrap();
    }
    return true;
}

std::string CloudConfigPublishImplementation::b64Encode(std::string const& raw) {
    return geode::utils::base64::encode(raw, geode::utils::base64::Base64Variant::Normal);
}

bool CloudConfigPublishImplementation::b64Decode(std::string const& b64, std::string& out,
                                                 std::string& err) {
    auto decoded =
        geode::utils::base64::decodeString(b64, geode::utils::base64::Base64Variant::Normal);
    if (decoded.isErr()) {
        err = "base64 decode failed";
        return false;
    }
    out = decoded.unwrap();
    return true;
}

matjson::Value
CloudConfigPublishImplementation::packEntryObject(CloudConfigPublish::PackMeta const& meta,
                                                  std::string const& sha256, size_t size) {
    matjson::Value tags = matjson::Value::array();
    for (auto const& t : meta.tags)
        tags.push(t);
    if (meta.tags.empty())
        tags.push("shared");

    matjson::Value valuePointer = matjson::Value::object();
    valuePointer["id"] = meta.id;
    valuePointer["name"] = meta.name;
    valuePointer["author"] = meta.author;
    valuePointer["description"] = meta.description;
    valuePointer["tags"] = tags;
    valuePointer["minModVersion"] = meta.minModVersion.empty() ? "1.4.1" : meta.minModVersion;
    valuePointer["bundleVersion"] = meta.bundleVersion <= 0 ? 2 : meta.bundleVersion;
    valuePointer["sha256"] = sha256;
    valuePointer["path"] = std::string("packs/") + meta.id + ".json";
    valuePointer["size"] = static_cast<int64_t>(size);
    return valuePointer;
}

bool CloudConfigPublish::isValidRepoSlug(std::string const& slug) {
    // owner/name — alnum, dash, underscore, dot
    auto const slash = slug.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= slug.size())
        return false;
    if (slug.find('/', slash + 1) != std::string::npos)
        return false;
    for (char const character : slug) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter) || character == '-' || character == '_' ||
            character == '.' || character == '/')
            continue;
        return false;
    }
    return true;
}

std::string CloudConfigPublish::contentsApiUrl(std::string const& repo, std::string const& path) {
    return fmt::format("https://api.github.com/repos/{}/contents/{}", repo, path);
}

std::string CloudConfigPublish::isoNowUtc() {
    using clock = std::chrono::system_clock;
    auto const timestamp = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &timestamp);
#else
    gmtime_r(&timestamp, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

bool CloudConfigPublish::readEnableSetting() {
    // No in-mod maintainer settings — public Share goes through Vercel submit.
    return false;
}

std::string CloudConfigPublish::readTokenSetting() {
    return {};
}

std::string CloudConfigPublish::readRepoSetting() {
    return CloudConfigPublishImplementation::kDefaultRepo;
}

bool CloudConfigPublish::isConfigured() {
    return false;
}

CloudConfigPublish::Result CloudConfigPublish::publishPack(PackMeta const& metaIn,
                                                           std::string const& bundleJson) {
    Result result{};
    result.attempted = true;
    result.repo = readRepoSetting();

    if (!readEnableSetting()) {
        result.attempted = false;
        result.error = "publish disabled";
        return result;
    }
    std::string const token = readTokenSetting();
    if (token.empty()) {
        result.error = "cloud-publish-token empty";
        return result;
    }
    if (!isValidRepoSlug(result.repo)) {
        result.error = "invalid cloud-publish-repo";
        return result;
    }
    if (!CloudConfigStore::isValidPackId(metaIn.id)) {
        result.error = "invalid pack id";
        return result;
    }
    if (bundleJson.empty() || bundleJson.size() > CloudConfigStore::kMaxPackBytes) {
        result.error = "bundle size invalid";
        return result;
    }
    auto bundleParsed = matjson::parse(bundleJson);
    if (bundleParsed.isErr()) {
        result.error = "bundle json invalid";
        return result;
    }

    PackMeta meta = metaIn;
    std::string const packRel = std::string("packs/") + meta.id + ".json";
    result.packPath = packRel;
    std::string const sha256 = CloudConfigSha256::hashHex(bundleJson);
    size_t const size = bundleJson.size();

    // --- PUT pack file ---
    {
        std::string packSha;
        matjson::Value existing;
        int code = 0;
        std::string err;
        std::string const packUrl = contentsApiUrl(result.repo, packRel);
        if (CloudConfigPublishImplementation::httpGetJson(packUrl, token, existing, code, err)) {
            if (existing.contains("sha") && existing["sha"].isString()) {
                packSha = existing["sha"].asString().unwrapOrDefault();
            }
        } else if (code != 404 && err != "not found") {
            result.error = std::string("pack GET: ") + err;
            return result;
        }

        matjson::Value putBody = matjson::Value::object();
        putBody["message"] = fmt::format("cloud: upsert pack {}", meta.id);
        putBody["content"] = CloudConfigPublishImplementation::b64Encode(bundleJson);
        putBody["branch"] = CloudConfigPublishImplementation::kBranch;
        if (!packSha.empty()) {
            putBody["sha"] = packSha;
            result.created = false;
        } else {
            result.created = true;
        }

        matjson::Value putRes;
        if (!CloudConfigPublishImplementation::httpPutJson(packUrl, token, putBody, putRes, code,
                                                           err)) {
            result.error = std::string("pack PUT: ") + err;
            return result;
        }
        if (putRes.contains("content") && putRes["content"].isObject()) {
            auto const& c = putRes["content"];
            if (c.contains("html_url") && c["html_url"].isString()) {
                result.commitUrl = c["html_url"].asString().unwrapOrDefault();
            }
        }
        if (result.commitUrl.empty() && putRes.contains("commit") && putRes["commit"].isObject()) {
            auto const& c = putRes["commit"];
            if (c.contains("html_url") && c["html_url"].isString()) {
                result.commitUrl = c["html_url"].asString().unwrapOrDefault();
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
        std::string const indexUrl = contentsApiUrl(result.repo, "index.json");
        if (CloudConfigPublishImplementation::httpGetJson(indexUrl, token, existing, code, err)) {
            if (existing.contains("sha") && existing["sha"].isString()) {
                indexSha = existing["sha"].asString().unwrapOrDefault();
            }
            if (existing.contains("content") && existing["content"].isString()) {
                std::string decoded;
                std::string decErr;
                auto b64 = existing["content"].asString().unwrapOrDefault();
                // GitHub may insert newlines in base64.
                b64.erase(std::remove(b64.begin(), b64.end(), '\n'), b64.end());
                if (CloudConfigPublishImplementation::b64Decode(b64, decoded, decErr)) {
                    auto parsed = matjson::parse(decoded);
                    if (parsed.isOk())
                        indexRoot = parsed.unwrap();
                }
            }
        } else if (code != 404 && err != "not found") {
            result.error = std::string("index GET: ") + err;
            return result;
        }

        if (!indexRoot.isObject())
            indexRoot = matjson::Value::object();
        indexRoot["schema"] = 1;
        indexRoot["updatedAt"] = isoNowUtc();

        matjson::Value packsArr = matjson::Value::array();
        if (indexRoot.contains("packs") && indexRoot["packs"].isArray()) {
            packsArr = indexRoot["packs"];
        }

        matjson::Value newEntry =
            CloudConfigPublishImplementation::packEntryObject(meta, sha256, size);
        bool replaced = false;
        matjson::Value next = matjson::Value::array();
        auto const arr = packsArr.asArray().unwrap();
        for (auto const& item : arr) {
            if (item.isObject() && item.contains("id") && item["id"].isString() &&
                item["id"].asString().unwrapOrDefault() == meta.id) {
                next.push(newEntry);
                replaced = true;
            } else {
                next.push(item);
            }
        }
        if (!replaced)
            next.push(newEntry);
        indexRoot["packs"] = next;

        // Validate before write.
        {
            CloudConfigIndex::Catalog category{};
            std::string valErr;
            if (!CloudConfigIndex::parse(indexRoot.dump(), category, valErr)) {
                result.error = std::string("index validate: ") + valErr;
                return result;
            }
        }

        matjson::Value putBody = matjson::Value::object();
        putBody["message"] = fmt::format("cloud: index upsert {}", meta.id);
        putBody["content"] = CloudConfigPublishImplementation::b64Encode(indexRoot.dump());
        putBody["branch"] = CloudConfigPublishImplementation::kBranch;
        if (!indexSha.empty())
            putBody["sha"] = indexSha;

        matjson::Value putRes;
        if (!CloudConfigPublishImplementation::httpPutJson(indexUrl, token, putBody, putRes, code,
                                                           err)) {
            result.error = std::string("index PUT: ") + err;
            return result;
        }
    }

    result.ok = true;
    if (result.commitUrl.empty()) {
        result.commitUrl = fmt::format("https://github.com/{}/blob/{}/{}", result.repo,
                                       CloudConfigPublishImplementation::kBranch, packRel);
    }
    return result;
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
    expect(contentsApiUrl("o/r", "index.json").find("api.github.com") != std::string::npos,
           "api-url");

    // No network in selfCheck — configured() may be false without token.
    expect(!publishPack({}, "{}").ok, "reject-empty-meta");

    if (fails == 0) {
        report = "CloudConfigPublish selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigPublish selfCheck fails: ") + report;
    return false;
}
