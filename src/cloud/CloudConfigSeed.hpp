#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSEED_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSEED_HPP

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

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSEED_HPP
