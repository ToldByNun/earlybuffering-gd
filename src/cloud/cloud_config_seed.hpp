#ifndef CLOUD_CONFIG_SEED_HPP
#define CLOUD_CONFIG_SEED_HPP

class CloudConfigSeedEmbedded;

#include <filesystem>
#include <string>

// Bundled offline catalog under Mod resources: cloud-seed/index.json + packs/.
class CloudConfigSeed {
public:
    static std::filesystem::path rootDir();
    static std::filesystem::path indexPath();
    static std::filesystem::path packPath(std::string const& id);

    static bool readIndex(std::string& out, std::string& err);
    static bool readPack(std::string const& id, std::string& out, std::string& err);
    static bool available();

    static bool selfCheck(std::string& report);

private:
    CloudConfigSeed() = default;
};

#include "cloud_config_index.hpp"
#include "cloud_config_sha256.hpp"
#include "cloud_config_store.hpp"
#include <Geode/Geode.hpp>
#include <fstream>

class CloudConfigSeedEmbedded {
public:
    static char const* indexJson();

    static char const* safeDefaultsPackJson();

private:
    CloudConfigSeedEmbedded() = default;
};

#endif
