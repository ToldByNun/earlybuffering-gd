#ifndef EARLYBUFFER_CORE_LEVELCACHEINDEX_HPP
#define EARLYBUFFER_CORE_LEVELCACHEINDEX_HPP

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>

// Paths + meta.json for {saveDir}/level-cache/{fullKey}/.
// Startpos shards (sp_*) land in a later change; this sheet is root-key only.
class LevelCacheIndex {
public:
    static constexpr int kSchema = 1;

    struct Meta {
        int schema = kSchema;
        std::string fullKey;
        int startXq = 0;
        std::string modVersion;
        std::string gdVersion;
        double tps = 240.0;
        int bakeRadius = 0;
        std::string settingsFp;
        std::string contentRev;
        int objectCount = 0;
        std::string updatedAt;
        bool stale = false;
        int64_t bytesApprox = 0;
    };

    static std::filesystem::path rootDir();
    static std::filesystem::path levelDir(std::string const& fullKey);
    static std::filesystem::path metaPath(std::string const& fullKey);
    static std::filesystem::path clicksPath(
        std::string const& fullKey, std::string const& method
    );
    static std::filesystem::path routePath(std::string const& fullKey);
    static std::filesystem::path triggersPath(std::string const& fullKey);
    static std::filesystem::path profilePath(std::string const& fullKey);

    // Legacy flat files (kept readable until callers fully switch).
    static std::filesystem::path legacyBakesDir();
    static std::filesystem::path legacyBakePath(std::string const& fullKey);
    static std::filesystem::path legacyTriggersDir();
    static std::filesystem::path legacyTriggerPath(std::string const& fullKey);
    static std::filesystem::path legacyLevelsDir();
    static std::filesystem::path legacyProfilePath(std::string const& fullKey);

    static bool ensureRoot(std::string& err);
    static bool ensureLevelDir(std::string const& fullKey, std::string& err);

    static geode::Result<> writeMeta(Meta const& meta);
    static geode::Result<Meta> readMeta(std::string const& fullKey);

    // Copy legacy bake/triggers/profile into cache when cache dir is absent.
    // Leaves legacy files in place (read fallback). Returns true if migration
    // ran or cache already present; false only on hard failure.
    static bool migrateLegacyIfNeeded(std::string const& fullKey, std::string& log);

    static char const* clicksFileName(std::string const& method);
    static std::string normalizeMethod(std::string const& method);

    static bool selfCheck(std::string& report);

private:
    LevelCacheIndex() = delete;

    static std::string nowUtcIso();
    static geode::Result<> copyFileIfPresent(
        std::filesystem::path const& from,
        std::filesystem::path const& to
    );
    static int64_t dirBytesApprox(std::filesystem::path const& dir);
};

#endif // EARLYBUFFER_CORE_LEVELCACHEINDEX_HPP
