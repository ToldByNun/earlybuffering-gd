#include "cloud_config_submit.hpp"
#include "cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <chrono>

bool CloudConfigSubmitImplementation::parseHttpsHost(std::string const& url, std::string& hostOut) {
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
    for (char& c : hostOut)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return !hostOut.empty();
}

bool CloudConfigSubmitImplementation::hostAllowed(std::string const& host) {
    if (host == "eb-cloud-submit.toldbynun.workers.dev")
        return true;
    if (host == "earlybuffer-config.vercel.app")
        return true;
    if (host.size() > 12 && host.compare(host.size() - 12, 12, ".workers.dev") == 0) {
        return true;
    }
    // Vercel hobby / production hosts (set full URL in cloud-submit-url).
    if (host.size() > 11 && host.compare(host.size() - 11, 11, ".vercel.app") == 0) {
        return true;
    }
    return false;
}

char const* CloudConfigSubmit::defaultSubmitUrl() {
    return "https://earlybuffer-config.vercel.app/api/submit";
}

bool CloudConfigSubmit::readEnableSetting() {
    return true;
}

std::string CloudConfigSubmit::effectiveSubmitUrl() {
    return defaultSubmitUrl();
}

bool CloudConfigSubmit::isAllowedSubmitUrl(std::string const& url) {
    std::string host;
    if (!(!CloudConfigSubmitImplementation::parseHttpsHost(url, host))) {
        return CloudConfigSubmitImplementation::hostAllowed(host);
    }
    return false;
}

CloudConfigSubmit::Result CloudConfigSubmit::submit(CloudConfigPublish::PackMeta const& meta,
                                                    std::string const& bundleJson) {
    Result result{};
    result.attempted = true;
    result.urlUsed = effectiveSubmitUrl();

    if (!readEnableSetting()) {
        result.attempted = false;
        result.error = "submit disabled";
        return result;
    }
    if (!CloudConfigStore::isValidPackId(meta.id)) {
        result.error = "invalid pack id";
        return result;
    }
    if (bundleJson.empty() || bundleJson.size() > CloudConfigStore::kMaxPackBytes) {
        result.error = "bundle size invalid";
        return result;
    }
    if (!isAllowedSubmitUrl(result.urlUsed)) {
        result.error = "submit url not allowed";
        return result;
    }

    matjson::Value tags = matjson::Value::array();
    for (auto const& t : meta.tags)
        tags.push(t);
    if (meta.tags.empty())
        tags.push("shared");

    matjson::Value body = matjson::Value::object();
    body["schema"] = 1;
    body["id"] = meta.id;
    body["name"] = meta.name;
    body["author"] = meta.author;
    body["description"] = meta.description;
    body["tags"] = tags;
    body["minModVersion"] = meta.minModVersion.empty() ? "1.4.1" : meta.minModVersion;
    body["bundleVersion"] = meta.bundleVersion <= 0 ? 2 : meta.bundleVersion;
    auto bundleParsed = matjson::parse(bundleJson);
    if (bundleParsed.isErr()) {
        result.error = "bundle json invalid";
        return result;
    }
    body["bundle"] = bundleParsed.unwrap();

    geode::utils::web::WebRequest req;
    req.userAgent("EarlyBuffer-CloudSubmit/1");
    req.timeout(std::chrono::seconds(30));
    req.followRedirects(false);
    req.certVerification(true);
    req.header("Accept", "application/json");
    req.header("Content-Type", "application/json");
    req.bodyJSON(body);

    geode::utils::web::WebResponse const resultValue = req.postSync(result.urlUsed);
    if (resultValue.cancelled()) {
        result.error = "cancelled";
        return result;
    }

    auto const text = resultValue.string();
    std::string resp;
    if (text.isOk())
        resp = text.unwrap();

    if (!resultValue.ok()) {
        result.error = fmt::format("http {}", resultValue.code());
        if (!resp.empty() && resp.size() < 400) {
            auto parsed = matjson::parse(resp);
            if (parsed.isOk() && parsed.unwrap().contains("error") &&
                parsed.unwrap()["error"].isString()) {
                result.error = parsed.unwrap()["error"].asString().unwrapOrDefault();
                if (result.error.empty())
                    result.error = fmt::format("http {}", resultValue.code());
            } else {
                result.error += std::string(" ") + resp;
            }
        }
        return result;
    }

    if (!resp.empty()) {
        auto parsed = matjson::parse(resp);
        if (parsed.isOk()) {
            auto const root = parsed.unwrap();
            if (root.contains("url") && root["url"].isString()) {
                result.prOrCommitUrl = root["url"].asString().unwrapOrDefault();
            } else if (root.contains("html_url") && root["html_url"].isString()) {
                result.prOrCommitUrl = root["html_url"].asString().unwrapOrDefault();
            }
        }
    }

    result.ok = true;
    return result;
}

bool CloudConfigSubmit::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::string(defaultSubmitUrl()).find("https://") == 0, "default-https");
    expect(isAllowedSubmitUrl(defaultSubmitUrl()), "allow-default");
    expect(!isAllowedSubmitUrl("http://eb-cloud-submit.toldbynun.workers.dev/submit"),
           "reject-http");
    expect(!isAllowedSubmitUrl("https://evil.example/submit"), "reject-host");
    expect(isAllowedSubmitUrl("https://eb-cloud-submit-demo.vercel.app/api/submit"),
           "allow-vercel");
    expect(!effectiveSubmitUrl().empty(), "effective-nonempty");

    // No network: disabled path / bad id.
    CloudConfigPublish::PackMeta bad{};
    expect(!submit(bad, "{}").ok, "reject-bad-id");

    if (fails == 0) {
        report = "CloudConfigSubmit selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigSubmit selfCheck fails: ") + report;
    return false;
}
