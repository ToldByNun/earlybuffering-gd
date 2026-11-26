#ifndef TRIGGER_WORLD_CODEC_HPP
#define TRIGGER_WORLD_CODEC_HPP

#include "../trigger_world_store.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

class EbTriggerWorldCodec {
public:
    static geode::Result<> writeFile(EbTriggerWorldStore const& owner);
    static geode::Result<> readFile(EbTriggerWorldStore& owner);

    // Disk helpers used by async load (no store mutation).
    static std::filesystem::path binaryPath(std::string const& key);
    static std::filesystem::path jsonPath(std::string const& key);

    // Parse into outData. Prefer .ebtw; fall back to JSON and set fromJson.
    // On JSON success also writes .ebtw and removes the JSON (migrate once).
    static geode::Result<> loadInto(std::string const& key, EbTriggerWorldData& outData,
                                    bool& fromJson, std::string& sourceTag);

    static geode::Result<> writeBinary(std::string const& key, EbTriggerWorldData const& data);
    static geode::Result<> readBinary(std::string const& key, EbTriggerWorldData& outData);
    static geode::Result<> readJson(std::string const& key, EbTriggerWorldData& outData);

private:
    EbTriggerWorldCodec() = delete;

    static constexpr std::uint32_t kBinaryVersion = 1;
};

#endif
