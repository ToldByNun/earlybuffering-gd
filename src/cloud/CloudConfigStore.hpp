#ifndef EARLYBUFFER_CLOUD_CLOUDCONFIGSTORE_HPP
#define EARLYBUFFER_CLOUD_CLOUDCONFIGSTORE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Disk layout under Mod saveDir/cloud-configs/:
//   index-cache.json, meta.json, active.json, packs/<id>.json, outbox/
class CloudConfigStore {
public:
    static constexpr size_t kMaxPackBytes = 256u * 1024u;
    static constexpr size_t kMaxIndexBytes = 512u * 1024u;
    static constexpr size_t kMaxIdLen = 64;

    struct ActiveInfo {
        std::string id;
        std::string sha256;
        std::string name;
        std::string appliedAt;
    };

    struct MetaInfo {
        bool lastRefreshOk = false;
        std::string lastError;
        std::string sourceUrl;
        std::string updatedAt;
    };

    static CloudConfigStore& get();

    std::filesystem::path rootDir() const;
    std::filesystem::path packsDir() const;
    std::filesystem::path outboxDir() const;
    std::filesystem::path indexCachePath() const;
    std::filesystem::path metaPath() const;
    std::filesystem::path activePath() const;
    // packs/<id>.json — empty path if id invalid.
    std::filesystem::path packPath(std::string const& id) const;

    // Creates cloud-configs + packs + outbox. false + err on failure.
    bool ensureDirs(std::string& err) const;

    // Pack id: [a-z0-9-]{1..64}, no leading/trailing '-'.
    static bool isValidPackId(std::string const& id);

    static bool readTextFile(
        std::filesystem::path const& path,
        std::string& out,
        size_t maxBytes,
        std::string& err
    );
    static bool writeTextFile(
        std::filesystem::path const& path,
        std::string const& body,
        std::string& err
    );

    bool readIndexCache(std::string& out, std::string& err) const;
    bool writeIndexCache(std::string const& body, std::string& err) const;

    bool readPack(std::string const& id, std::string& out, std::string& err) const;
    bool writePack(std::string const& id, std::string const& body, std::string& err) const;
    bool removePack(std::string const& id, std::string& err) const;
    bool hasPack(std::string const& id) const;
    std::vector<std::string> listCachedPackIds() const;

    bool readActive(ActiveInfo& out, std::string& err) const;
    bool writeActive(ActiveInfo const& info, std::string& err) const;
    bool clearActive(std::string& err) const;

    bool readMeta(MetaInfo& out, std::string& err) const;
    bool writeMeta(MetaInfo const& info, std::string& err) const;

    // Writes outbox/<stem>.json. stem sanitized; empty stem → "share".
    bool writeOutbox(
        std::string const& stem,
        std::string const& body,
        std::filesystem::path& outPath,
        std::string& err
    ) const;

    // Path composition + id rules; when Mod is live also ensureDirs + round-trip files.
    static bool selfCheck(std::string& report);

private:
    CloudConfigStore() = default;

    static std::string sanitizeStem(std::string const& stem);
};

#endif // EARLYBUFFER_CLOUD_CLOUDCONFIGSTORE_HPP
