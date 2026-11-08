#ifndef CLOUD_CONFIG_CLIENT_HPP
#define CLOUD_CONFIG_CLIENT_HPP

#include <cstddef>
#include <functional>
#include <string>

// Async HTTPS GET (Geode WebRequest getSync on a worker). Cancel drops late callbacks.
class CloudConfigClient {
public:
    static constexpr int kDefaultTimeoutSec = 20;

    class FetchResult {
    public:
        bool ok = false;
        bool cancelled = false;
        int httpCode = 0;
        std::string body;
        std::string error;
        std::string urlUsed;

    private:
    };

    using Callback = std::function<void(FetchResult const&)>;

    CloudConfigClient();
    ~CloudConfigClient();

    CloudConfigClient(CloudConfigClient const&) = delete;
    CloudConfigClient& operator=(CloudConfigClient const&) = delete;

    // Starts a GET. Cancels any in-flight request first.
    // maxBytes: reject oversized bodies after download.
    // customCatalogUrl: HostPolicy allow-context for non-allowlisted hosts.
    void getText(std::string const& url, size_t maxBytes, int timeoutSec,
                 std::string const& customCatalogUrl, Callback callback);

    void cancel();
    bool isBusy() const;

    // No network — cancel/busy + HostPolicy reject only.
    static bool selfCheck(std::string& report);

private:
    class Impl;
    Impl* m_impl = nullptr;
};

#include "cloud_config_host_policy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>
#include <thread>

class CloudConfigClientImplementation {
public:
    static CloudConfigClient::FetchResult cloudConfigFetchSync(std::string const& url, size_t limit,
                                                               int timeoutSec);

private:
    CloudConfigClientImplementation() = delete;
};

class CloudConfigClient::Impl {
public:
    uint64_t gen = 0;
    bool busy = false;

private:
};

#endif
