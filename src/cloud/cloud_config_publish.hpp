#ifndef CLOUD_CONFIG_PUBLISH_HPP
#define CLOUD_CONFIG_PUBLISH_HPP

#include <string>
#include <vector>

// Optional maintainer publish: GitHub Contents API → earlybuffer-configs.
// Requires local PAT setting (never bundled / never exported). No anonymous upload.
class CloudConfigPublish {
public:
    class Result {
    public:
        bool attempted = false;
        bool ok = false;
        bool created = false; // true = new file, false = updated existing
        std::string repo;
        std::string packPath;
        std::string commitUrl;
        std::string error;

    private:
    };

    class PackMeta {
    public:
        std::string id;
        std::string name;
        std::string author;
        std::string description;
        std::vector<std::string> tags;
        std::string minModVersion = "1.4.1";
        int bundleVersion = 2;

    private:
    };

    // True when enable + non-empty token + valid repo slug.
    static bool isConfigured();

    static std::string readTokenSetting();
    static std::string readRepoSetting();
    static bool readEnableSetting();

    // PUT packs/<id>.json then upsert index.json on branch main.
    // bundleJson = bare ConfigBundle document (not the share envelope).
    static Result publishPack(PackMeta const& meta, std::string const& bundleJson);

    static bool selfCheck(std::string& report);

private:
    CloudConfigPublish() = default;

    static bool isValidRepoSlug(std::string const& slug);
    static std::string contentsApiUrl(std::string const& repo, std::string const& path);
    static std::string isoNowUtc();
};

#include "cloud_config_index.hpp"
#include "cloud_config_sha256.hpp"
#include "cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

class CloudConfigPublishImplementation {
public:
    static bool httpGetJson(std::string const& url, std::string const& token, matjson::Value& out,
                            int& code, std::string& err);
    static bool httpPutJson(std::string const& url, std::string const& token,
                            matjson::Value const& body, matjson::Value& out, int& code,
                            std::string& err);
    static std::string b64Encode(std::string const& raw);
    static bool b64Decode(std::string const& b64, std::string& out, std::string& err);
    static matjson::Value packEntryObject(CloudConfigPublish::PackMeta const& meta,
                                          std::string const& sha256, size_t size);

private:
    friend class CloudConfigPublish;

    inline static constexpr char const* kDefaultRepo = "ToldByNun/earlybuffer-configs";
    inline static constexpr char const* kBranch = "main";
    inline static constexpr int kTimeoutSec = 25;

    CloudConfigPublishImplementation() = delete;
};

#endif
