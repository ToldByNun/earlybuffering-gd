#ifndef CLOUD_CONFIG_SHA256_HPP
#define CLOUD_CONFIG_SHA256_HPP

class EbCloudConfigSha256Core;

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Portable SHA-256 → lowercase hex (64 chars). No external crypto dep.
class EbCloudConfigSha256 {
public:
    static std::string hashHex(std::string_view bytes);
    static std::string hashHex(void const* data, size_t length);

    // Known-answer + length checks (no disk).
    static bool selfCheck(std::string& report);

private:
    EbCloudConfigSha256() = default;
};

#include <array>
#include <cstring>

class EbCloudConfigSha256Core {
public:
    EbCloudConfigSha256Core();

    void reset();

    void update(uint8_t const* data, size_t length);

    void finalize(uint8_t output[32]);

private:
    static uint32_t rotr(uint32_t x, uint32_t count);

    void transform(uint8_t const* chunk);

    std::array<uint32_t, 8> m_hashState{};
    uint64_t m_totalBytes = 0;
    size_t m_bufferSize = 0;
    std::array<uint8_t, 64> m_buffer{};
};

#endif
