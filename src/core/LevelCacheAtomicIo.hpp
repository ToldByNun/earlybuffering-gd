#ifndef EARLYBUFFER_CORE_LEVELCACHEATOMICIO_HPP
#define EARLYBUFFER_CORE_LEVELCACHEATOMICIO_HPP

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>

// Atomic JSON write: path.tmp → flush → rename over path.
// Orphan .tmp files are ignored on read (never treated as the real file).
class LevelCacheAtomicIo {
public:
    static geode::Result<> writeJson(
        std::filesystem::path const& path,
        matjson::Value const& value
    );

    // Missing / corrupt / orphan-tmp-only → Err. Unknown JSON keys left to caller.
    static geode::Result<matjson::Value> readJson(std::filesystem::path const& path);

    // True when a regular file exists at path (not .tmp).
    static bool existsRegular(std::filesystem::path const& path);

    static bool selfCheck(std::string& report);

private:
    LevelCacheAtomicIo() = delete;

    static std::filesystem::path tmpPathFor(std::filesystem::path const& path);
    static geode::Result<> renameReplace(
        std::filesystem::path const& tmp,
        std::filesystem::path const& path
    );
};

#endif // EARLYBUFFER_CORE_LEVELCACHEATOMICIO_HPP
