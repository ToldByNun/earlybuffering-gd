#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSHA256_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSHA256_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Portable SHA-256 → lowercase hex (64 chars). No external crypto dep.
class CloudConfigSha256 {
public:
    static std::string hashHex(std::string_view bytes);
    static std::string hashHex(void const* data, size_t len);

    // Known-answer + length checks (no disk).
    static bool selfCheck(std::string& report);

private:
    CloudConfigSha256() = default;
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSHA256_HPP
