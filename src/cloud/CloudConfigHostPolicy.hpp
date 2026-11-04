#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGHOSTPOLICY_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGHOSTPOLICY_HPP

#include <string>

// Default catalog URLs, HTTPS allowlist, relative pack path → absolute URL.
class CloudConfigHostPolicy {
public:
    // Built-in catalog (jsDelivr primary, GitHub raw fallback).
    static char const* defaultIndexUrl();
    static char const* fallbackIndexUrl();

    // Empty custom → defaultIndexUrl(). Non-empty must pass isAllowedUrl.
    static std::string effectiveIndexUrl(std::string const& customSetting);

    // HTTPS only; host must be allowlisted or equal to custom catalog host.
    static bool isAllowedUrl(
        std::string const& url,
        std::string const& customCatalogUrl = {}
    );

    // Resolve packs/foo.json against the directory of catalogUrl.
    // Rejects absolute URLs, scheme, '..', empty, or non-packs/ paths.
    static bool resolvePackUrl(
        std::string const& catalogUrl,
        std::string const& relativePath,
        std::string& outUrl,
        std::string& err
    );

    static bool selfCheck(std::string& report);

private:
    CloudConfigHostPolicy() = default;

    static bool parseHttpsUrl(
        std::string const& url,
        std::string& hostOut,
        std::string& pathOut,
        std::string& err
    );
    static bool isAllowlistedHost(std::string const& host);
    static std::string hostOf(std::string const& url);
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGHOSTPOLICY_HPP
