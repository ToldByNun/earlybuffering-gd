#ifndef CLOUD_CONFIG_INDEX_HPP
#define CLOUD_CONFIG_INDEX_HPP

#include <cstddef>
#include <string>
#include <vector>

// Parsed / validated cloud catalog index.json (schema 1).
class EbCloudConfigIndex {
public:
    static constexpr int kSchema = 1;

    class PackEntry {
    public:
        std::string id;
        std::string name;
        std::string author;
        std::string description;
        std::vector<std::string> tags;
        std::string minModVersion; // e.g. "1.4.1" or "v1.4.1"
        int bundleVersion = 2;
        std::string sha256; // lowercase hex, 64 chars
        std::string path;   // relative, must start with packs/
        size_t size = 0;

    private:
    };

    class Catalog {
    public:
        int schema = 0;
        std::string updatedAt;
        std::vector<PackEntry> packs;

    private:
    };

    // Parse + validate. On failure out is cleared and err set.
    static bool parse(std::string const& json, Catalog& out, std::string& err);

    // True if liveModVersion >= entry.minModVersion (missing/empty min → true).
    static bool meetsMinModVersion(PackEntry const& entry, std::string const& liveModVersion);

    static PackEntry const* findById(Catalog const& catalog, std::string const& id);

    static bool selfCheck(std::string& report);

private:
    EbCloudConfigIndex() = default;

    static bool validatePack(PackEntry const& p, std::string& err);
    static bool isLowerHexSha256(std::string const& hex);
    static bool isSafeRelativePackPath(std::string const& path);
};

#endif
