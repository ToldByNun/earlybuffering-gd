#ifndef EARLYBUFFER_CORE_LEVELFINGERPRINT_HPP
#define EARLYBUFFER_CORE_LEVELFINGERPRINT_HPP

#include "LevelCacheIndex.hpp"

#include <string>

class GJGameLevel;
class PlayLayer;

// Bake/cache fingerprint helpers: startX quantization + settingsFp + contentRev.
class LevelFingerprint {
public:
    // One block ≈ 30px (GD grid).
    static constexpr float kBlockPx = 30.f;

    static int startXq(float startX);
    static std::string shardFolderName(int startXq);

    // Short hex of bake-relevant settings (stable across runs with same values).
    static std::string settingsFp();

    static std::string contentRev(GJGameLevel* level);
    static int objectCount(PlayLayer* layer);
    static int objectCount(GJGameLevel* level);

    // Fill identity fields on meta (does not write disk).
    static void fillIdentity(
        LevelCacheIndex::Meta& meta,
        GJGameLevel* level,
        PlayLayer* layer,
        float startX,
        double tps,
        int bakeRadius
    );

    static bool selfCheck(std::string& report);

private:
    LevelFingerprint() = delete;
};

#endif // EARLYBUFFER_CORE_LEVELFINGERPRINT_HPP
