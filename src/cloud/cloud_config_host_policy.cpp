#include "cloud_config_host_policy.hpp"
#include <algorithm>
#include <cctype>

char const* CloudConfigHostPolicy::defaultIndexUrl() {
    return "https://cdn.jsdelivr.net/gh/ToldByNun/earlybuffer-configs@main/index.json";
}

char const* CloudConfigHostPolicy::fallbackIndexUrl() {
    return "https://raw.githubusercontent.com/ToldByNun/earlybuffer-configs/main/index.json";
}

std::string CloudConfigHostPolicy::hostOf(std::string const& url) {
    std::string host;
    std::string path;
    std::string err;
    if (!(!parseHttpsUrl(url, host, path, err))) {
        return host;
    }
    return {};
}

bool CloudConfigHostPolicy::isAllowlistedHost(std::string const& host) {
    return host == "cdn.jsdelivr.net" || host == "raw.githubusercontent.com";
}

bool CloudConfigHostPolicy::parseHttpsUrl(std::string const& url, std::string& hostOut,
                                          std::string& pathOut, std::string& err) {
    hostOut.clear();
    pathOut.clear();
    err.clear();
    static char const* kPrefix = "https://";
    if (url.size() < 8 || url.compare(0, 8, kPrefix) != 0) {
        err = "url must be https://";
        return false;
    }
    if (url.find('@') != std::string::npos) {
        err = "url userinfo not allowed";
        return false;
    }
    size_t const hostStart = 8;
    size_t hostEnd = url.find_first_of("/?#", hostStart);
    if (hostEnd == std::string::npos)
        hostEnd = url.size();
    if (hostEnd <= hostStart) {
        err = "url host empty";
        return false;
    }
    hostOut = url.substr(hostStart, hostEnd - hostStart);
    // Reject IPv4 / IPv6 / empty labels.
    if (hostOut.find(':') != std::string::npos) {
        err = "url host port/IPv6 not allowed";
        return false;
    }
    bool digitOnly = !hostOut.empty();
    bool hasDot = false;
    for (char const character : hostOut) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        if (!(std::isalnum(unsignedCharacter) || character == '-' || character == '.')) {
            err = "url host invalid char";
            return false;
        }
        if (character == '.')
            hasDot = true;
        if (!std::isdigit(unsignedCharacter) && character != '.')
            digitOnly = false;
    }
    if (digitOnly && hasDot) {
        err = "url IP host not allowed";
        return false;
    }
    if (hostEnd < url.size() && url[hostEnd] == '/') {
        pathOut = url.substr(hostEnd);
    } else if (hostEnd < url.size()) {
        // query/fragment without path
        pathOut = "/";
    } else {
        pathOut = "/";
    }
    // Strip query/fragment from path for resolve base.
    auto const queryOffset = pathOut.find_first_of("?#");
    if (queryOffset != std::string::npos)
        pathOut.resize(queryOffset);
    return true;
}

bool CloudConfigHostPolicy::isAllowedUrl(std::string const& url,
                                         std::string const& customCatalogUrl) {
    std::string host;
    std::string path;
    std::string err;
    if (!parseHttpsUrl(url, host, path, err))
        return false;
    if (isAllowlistedHost(host))
        return true;
    if (customCatalogUrl.empty())
        return false;
    std::string customHost = hostOf(customCatalogUrl);
    return !customHost.empty() && host == customHost;
}

std::string CloudConfigHostPolicy::effectiveIndexUrl(std::string const& customSetting) {
    if (customSetting.empty()) {
        return std::string(defaultIndexUrl());
    }
    if (!isAllowedUrl(customSetting, customSetting)) {
        return std::string(defaultIndexUrl());
    }
    return customSetting;
}

bool CloudConfigHostPolicy::resolvePackUrl(std::string const& catalogUrl,
                                           std::string const& relativePath, std::string& outUrl,
                                           std::string& err) {
    outUrl.clear();
    err.clear();
    if (relativePath.empty()) {
        err = "pack path empty";
        return false;
    }
    if (relativePath.find("://") != std::string::npos || relativePath.front() == '/' ||
        relativePath.find("..") != std::string::npos) {
        err = "pack path must be relative under packs/";
        return false;
    }
    if (relativePath.rfind("packs/", 0) != 0) {
        err = "pack path must start with packs/";
        return false;
    }
    for (char const character : relativePath) {
        unsigned char const unsignedCharacter = static_cast<unsigned char>(character);
        bool const ok = std::isalnum(unsignedCharacter) || character == '-' || character == '_' ||
                        character == '.' || character == '/';
        if (!ok) {
            err = "pack path invalid char";
            return false;
        }
    }

    std::string host;
    std::string path;
    if (!parseHttpsUrl(catalogUrl, host, path, err)) {
        return false;
    }
    // Directory of catalog file: drop trailing filename.
    std::string directory = path;
    auto const slash = directory.find_last_of('/');
    if (slash == std::string::npos) {
        directory = "/";
    } else {
        directory.resize(slash + 1);
    }
    outUrl = std::string("https://") + host + directory + relativePath;
    if (!isAllowedUrl(outUrl, catalogUrl)) {
        err = "resolved pack url not allowed";
        outUrl.clear();
        return false;
    }
    return true;
}

bool CloudConfigHostPolicy::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::string(defaultIndexUrl()).find("https://") == 0, "default-https");
    expect(std::string(fallbackIndexUrl()).find("https://") == 0, "fallback-https");
    expect(isAllowedUrl(defaultIndexUrl()), "allow-jsdelivr");
    expect(isAllowedUrl(fallbackIndexUrl()), "allow-raw");
    expect(!isAllowedUrl("http://cdn.jsdelivr.net/x"), "reject-http");
    expect(!isAllowedUrl("https://evil.example/x"), "reject-other-host");
    expect(!isAllowedUrl("https://user@cdn.jsdelivr.net/x"), "reject-userinfo");
    expect(!isAllowedUrl("https://1.2.3.4/x"), "reject-ip");

    std::string custom = "https://configs.example.com/index.json";
    expect(isAllowedUrl(custom, custom), "allow-custom-same");
    expect(isAllowedUrl("https://configs.example.com/packs/a.json", custom), "allow-custom-pack");
    expect(!isAllowedUrl("https://other.example.com/packs/a.json", custom), "reject-custom-other");

    expect(effectiveIndexUrl("") == defaultIndexUrl(), "effective-default");
    expect(effectiveIndexUrl(custom) == custom, "effective-custom");
    expect(effectiveIndexUrl("http://bad.example/x") == defaultIndexUrl(),
           "effective-bad-fallback");

    std::string out;
    std::string err;
    expect(resolvePackUrl(defaultIndexUrl(), "packs/safe-defaults.json", out, err), "resolve-ok");
    expect(out.find("packs/safe-defaults.json") != std::string::npos, "resolve-suffix");
    expect(isAllowedUrl(out), "resolve-allowed");
    expect(out.find("earlybuffer-configs@main/packs/") != std::string::npos, "resolve-keeps-base");
    expect(!resolvePackUrl(defaultIndexUrl(), "../etc/passwd", out, err), "resolve-dotdot");
    expect(!resolvePackUrl(defaultIndexUrl(), "/abs.json", out, err), "resolve-abs");
    expect(!resolvePackUrl(defaultIndexUrl(), "https://evil/x", out, err), "resolve-abs-url");
    expect(!resolvePackUrl(defaultIndexUrl(), "other/x.json", out, err), "resolve-not-packs");

    if (fails == 0) {
        report = "CloudConfigHostPolicy selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigHostPolicy selfCheck fails: ") + report;
    return false;
}
