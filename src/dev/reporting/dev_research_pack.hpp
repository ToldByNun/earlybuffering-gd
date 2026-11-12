#ifndef DEV_RESEARCH_PACK_HPP
#define DEV_RESEARCH_PACK_HPP

#include <filesystem>
#include <string>
#include <vector>

// One-click Dev Lab research folder under mod save dir /research/.
// Bundles session JSON, report overlay + jsonLine, fingerprint, config (no keybinds).
class EbDevResearchPack {
public:
    static constexpr int kManifestSchema = 1;

    class Result {
    public:
        std::filesystem::path dir;
        std::vector<std::string> files;
        bool hasSession = false;
        bool hasReport = false;
        bool hasConfig = false;

    private:
    };

    // Writes research/research_<stamp>/ and fills out. errMsg on failure.
    static bool exportPack(Result& out, std::string& errMsg);

    static std::filesystem::path researchRoot();
    static bool selfCheck(std::string& report);

private:
    EbDevResearchPack() = delete;
};

#endif
