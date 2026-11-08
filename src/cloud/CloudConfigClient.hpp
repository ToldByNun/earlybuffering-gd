#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGCLIENT_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGCLIENT_HPP

#include <cstddef>
#include <functional>
#include <string>

// Async HTTPS GET (Geode WebRequest getSync on a worker). Cancel drops late callbacks.
class CloudConfigClient {
public:
    static constexpr int kDefaultTimeoutSec = 20;

    struct FetchResult {
        bool ok = false;
        bool cancelled = false;
        int httpCode = 0;
        std::string body;
        std::string error;
        std::string urlUsed;
    };

    using Callback = std::function<void(FetchResult const&)>;

    CloudConfigClient();
    ~CloudConfigClient();

    CloudConfigClient(CloudConfigClient const&) = delete;
    CloudConfigClient& operator=(CloudConfigClient const&) = delete;

    // Starts a GET. Cancels any in-flight request first.
    // maxBytes: reject oversized bodies after download.
    // customCatalogUrl: HostPolicy allow-context for non-allowlisted hosts.
    void getText(
        std::string const& url,
        size_t maxBytes,
        int timeoutSec,
        std::string const& customCatalogUrl,
        Callback cb
    );

    void cancel();
    bool isBusy() const;

    // No network — cancel/busy + HostPolicy reject only.
    static bool selfCheck(std::string& report);

private:
    class Impl;
    Impl* m_impl = nullptr;
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGCLIENT_HPP
