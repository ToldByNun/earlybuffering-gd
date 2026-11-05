#ifndef EARLYBUFFER_CORE_CLICKBAKESTORE_HPP
#define EARLYBUFFER_CORE_CLICKBAKESTORE_HPP

#include <Geode/Geode.hpp>
#include <string>

struct BakeMapData;

// Per-source click files under level-cache/{fullKey}/clicks-{method}.json.
// BakeMap load/save goes through here; legacy bakes/{key}.json stays readable.
class ClickBakeStore {
public:
    static constexpr int kFileSchema = 1;

    // Load merged clicks for a level key (migrate legacy → cache first).
    // Empty sources → Ok with empty clicks (caller may mark loaded).
    static geode::Result<> loadMerged(
        std::string const& fullKey,
        BakeMapData& out
    );

    // Write only clicks-{method}.json for data.method; refresh meta.json.
    // Does not delete other method files.
    static geode::Result<> saveMethod(BakeMapData const& data);

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
