#ifndef EARLYBUFFER_CORE_CLICKBAKESTORE_HPP
#define EARLYBUFFER_CORE_CLICKBAKESTORE_HPP

#include <Geode/Geode.hpp>
#include <string>

struct BakeMapData;

// Per-source click files under level-cache/{fullKey}/[sp_N/]clicks-{method}.json.
// BakeMap load/save goes through here; legacy bakes/{key}.json stays readable.
class ClickBakeStore {
public:
    static constexpr int kFileSchema = 1;

    // Load merged clicks for a level key + startpos shard (migrate legacy first).
    // Empty sources → Ok with empty clicks (caller may mark loaded).
    static geode::Result<> loadMerged(
        std::string const& fullKey,
        BakeMapData& out,
        int startXq = 0
    );

    // Write only clicks-{method}.json for data.method into the shard;
    // refresh shard meta.json. Does not delete other method files or shards.
    static geode::Result<> saveMethod(
        BakeMapData const& data,
        int startXq = 0
    );

    // Parse / emit one clicks-*.json body (shared with migration + selfCheck).
    static geode::Result<> parseClicksJson(
        matjson::Value const& root,
        BakeMapData& out
    );
    static matjson::Value clicksToJson(BakeMapData const& data);

    static bool selfCheck(std::string& report);

private:
    ClickBakeStore() = delete;

    static geode::Result<> readOneSource(
        std::filesystem::path const& path,
        BakeMapData& out
    );
    static void appendUniqueClicks(
        BakeMapData& dst,
        BakeMapData const& src
    );
};

#endif // EARLYBUFFER_CORE_CLICKBAKESTORE_HPP
