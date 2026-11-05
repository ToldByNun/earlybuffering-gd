#ifndef EARLYBUFFER_CORE_LEVELCACHEINDEX_HPP
#define EARLYBUFFER_CORE_LEVELCACHEINDEX_HPP

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

// Paths + meta.json for {saveDir}/level-cache/{fullKey}/[sp_{startXq}/].
// startXq==0 keeps files at the level root (legacy C2 layout).
class LevelCacheIndex {
public:
    static constexpr int kSchema = 1;
    // Soft disk budget for the whole level-cache tree.
    static constexpr int64_t kSoftCapBytes = 256LL * 1024 * 1024;

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
    // Root for startXq==0; levelDir/sp_N otherwise.
    static std::filesystem::path shardDir(std::string const& fullKey, int startXq = 0);

    static std::filesystem::path metaPath(std::string const& fullKey, int startXq = 0);
    static std::filesystem::path clicksPath(
        std::string const& fullKey,
        std::string const& method,
        int startXq = 0
    );
    static std::filesystem::path routePath(std::string const& fullKey, int startXq = 0);
    // Shared across startpos shards (level root only).
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
    static bool ensureShardDir(
        std::string const& fullKey, int startXq, std::string& err
    );

    // Writes meta at shard path from meta.startXq.
    static geode::Result<> writeMeta(Meta const& meta);
    static geode::Result<Meta> readMeta(
        std::string const& fullKey, int startXq = 0
    );

    // Copy legacy bake/triggers/profile into cache when cache dir is absent.
    // Leaves legacy files in place (read fallback). Returns true if migration
    // ran or cache already present; false only on hard failure.
    static bool migrateLegacyIfNeeded(std::string const& fullKey, std::string& log);

    static char const* clicksFileName(std::string const& method);
    static std::string normalizeMethod(std::string const& method);

    // Delete one level tree (all shards) or the entire level-cache root.
    static bool clearLevel(std::string const& fullKey, std::string& err);
    static bool clearAll(std::string& err);

    static int64_t totalBytesApprox();
    // Evict oldest level dirs by meta.updatedAt until under soft cap.
    // Returns number of level folders removed.
    static int evictLruIfOverCap();

    // Overlay fragment, e.g. "cache=12MB" or "cache=260MB LRU".
    static std::string overlayBit();

    static bool selfCheck(std::string& report);

private:
    LevelCacheIndex() = delete;

    static std::string nowUtcIso();
    static geode::Result<> copyFileIfPresent(
        std::filesystem::path const& from,
        std::filesystem::path const& to
    );
    static int64_t dirBytesApprox(std::filesystem::path const& dir);

    struct LevelEntry {
        std::string fullKey;
        std::string updatedAt;
        int64_t bytes = 0;
    };
    static std::vector<LevelEntry> listLevelEntries();
};

#endif // EARLYBUFFER_CORE_LEVELCACHEINDEX_HPP
