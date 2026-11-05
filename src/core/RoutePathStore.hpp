#ifndef EARLYBUFFER_CORE_ROUTEPATHSTORE_HPP
#define EARLYBUFFER_CORE_ROUTEPATHSTORE_HPP

#include "RoutePath.hpp"

#include <filesystem>
#include <string>

// In-memory + disk route.json under level-cache/{fullKey}/[sp_N/].
class RoutePathStore {
public:
    static RoutePathStore& get();

    void clear();
    bool loaded() const { return m_loaded; }
    RoutePath const& path() const { return m_path; }

    // Load shard route; missing file → clear + Ok(false loaded).
    geode::Result<> load(std::string const& fullKey, int startXq = 0);
    // Atomic write; updates in-memory copy on success.
    geode::Result<> save(
        std::string const& fullKey,
        RoutePath const& path,
        int startXq = 0
    );
    // Delete route.json if present (validate fail / stale).
    static bool removeFile(
        std::string const& fullKey, int startXq, std::string& err
    );

    static std::filesystem::path filePath(
        std::string const& fullKey, int startXq = 0
    );

    // Overlay fragment when loaded, e.g. "route=842".
    std::string overlayBit() const;

    static bool selfCheck(std::string& report);

private:
    RoutePathStore() = default;

    RoutePath m_path{};
    bool m_loaded = false;
    std::string m_fullKey;
    int m_startXq = 0;
};

#endif // EARLYBUFFER_CORE_ROUTEPATHSTORE_HPP
