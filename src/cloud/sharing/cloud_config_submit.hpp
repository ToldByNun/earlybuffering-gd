#ifndef CLOUD_CONFIG_SUBMIT_HPP
#define CLOUD_CONFIG_SUBMIT_HPP

#include "cloud_config_publish.hpp"
#include <string>

// Public share path: POST pack to a submit Worker (no client secret).
// The Worker holds the GitHub token and writes/PRs into earlybuffer-configs.
class EbCloudConfigSubmit {
public:
    class Result {
    public:
        bool attempted = false;
        bool ok = false;
        std::string urlUsed;
        std::string prOrCommitUrl;
        std::string error;

    private:
    };

    // Built-in Worker URL (override with cloud-submit-url). Empty setting → default.
    static char const* defaultSubmitUrl();

    static bool readEnableSetting();
    static std::string effectiveSubmitUrl();

    // HTTPS + host allowlist for submit (workers.dev / github.io / custom setting host).
    static bool isAllowedSubmitUrl(std::string const& url);

    // POST JSON body with pack meta + bare bundle. Sync (share confirm waits).
    static Result submit(EbCloudConfigPublish::PackMeta const& meta, std::string const& bundleJson);

    static bool selfCheck(std::string& report);

private:
    EbCloudConfigSubmit() = default;
};

#include "../cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <chrono>

class EbCloudConfigSubmitImplementation {
public:
    static bool parseHttpsHost(std::string const& url, std::string& hostOut);
    static bool hostAllowed(std::string const& host);

private:
    EbCloudConfigSubmitImplementation() = delete;
};

#endif
