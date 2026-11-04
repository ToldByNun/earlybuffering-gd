#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSUBMIT_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSUBMIT_HPP

#include "CloudConfigPublish.hpp"

#include <string>

// Public share path: POST pack to a submit Worker (no client secret).
// The Worker holds the GitHub token and writes/PRs into earlybuffer-configs.
class CloudConfigSubmit {
public:
    struct Result {
        bool attempted = false;
        bool ok = false;
        std::string urlUsed;
        std::string prOrCommitUrl;
        std::string error;
    };

    // Built-in Worker URL (override with cloud-submit-url). Empty setting → default.
    static char const* defaultSubmitUrl();

    static bool readEnableSetting();
    static std::string effectiveSubmitUrl();

    // HTTPS + host allowlist for submit (workers.dev / github.io / custom setting host).
    static bool isAllowedSubmitUrl(std::string const& url);

    // POST JSON body with pack meta + bare bundle. Sync (share confirm waits).
    static Result submit(
        CloudConfigPublish::PackMeta const& meta,
        std::string const& bundleJson
    );

    static bool selfCheck(std::string& report);

private:
    CloudConfigSubmit() = default;
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSUBMIT_HPP
