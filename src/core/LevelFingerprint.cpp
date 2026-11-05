#include "LevelFingerprint.hpp"

#include "LevelContentKey.hpp"
#include "../cloud/CloudConfigSha256.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <cmath>
#include <fmt/format.h>
#include <string>

using namespace geode::prelude;

int LevelFingerprint::startXq(float startX) {
    if (!std::isfinite(startX)) return 0;
    return static_cast<int>(std::floor(startX / kBlockPx));
}

std::string LevelFingerprint::shardFolderName(int startXq) {
    if (startXq == 0) return {};
    return fmt::format("sp_{}", startXq);
}

std::string LevelFingerprint::settingsFp() {
    // Bake/assist-relevant knobs only — UI/hotkeys excluded.
    StageConfig::refreshCache();
    std::string canon = fmt::format(
        "assist-mode={}\n"
        "bake-radius={}\n"
        "ticks-all={}\n"
        "ticks-wave={}\n"
        "window-unit={}\n"
        "death-slack={}\n"
        "look-ahead-cap={}\n"
        "sim-budget-us={}\n"
        "tick-budget={}\n"
        "robust-k={}\n"
        "robust-jitter={}\n"
        "disable-hus={}\n"
        "assist-orbs={}\n"
        "smart-depth={}\n"
        "smart-combos={}\n"
        "smart-horizon={}\n"
        "self-bake={}\n"
        "self-bake-normal={}\n"
        "self-bake-refine={}\n"
        "trigger-bake={}\n"
        "trigger-bake-skip-dynamic={}",
        StageConfig::assistModeName(StageConfig::assistMode()),
        StageConfig::bakeRadius(),
        StageConfig::ticksAll(),
        StageConfig::ticksWave(),
        StageConfig::windowUnitFrames() ? "frames" : "ticks",
        StageConfig::deathSlack(),
        StageConfig::lookAheadCap(),
        StageConfig::simBudgetUs(),
        StageConfig::tickBudgetPerPress(),
        StageConfig::robustK(),
        StageConfig::robustJitter() ? 1 : 0,
        StageConfig::disableHus() ? 1 : 0,
        StageConfig::assistOrbs() ? 1 : 0,
        StageConfig::smartDepth(),
        StageConfig::smartCombos(),
        StageConfig::smartHorizon(),
        StageConfig::selfBake() ? 1 : 0,
        StageConfig::selfBakeNormal() ? 1 : 0,
        StageConfig::selfBakeRefine() ? 1 : 0,
        StageConfig::triggerBake() ? 1 : 0,
        StageConfig::triggerBakeSkipDynamic() ? 1 : 0
    );
    auto const hex = CloudConfigSha256::hashHex(canon);
    if (hex.size() >= 8) return hex.substr(0, 8);
    return hex;
}

std::string LevelFingerprint::contentRev(GJGameLevel* level) {
    return LevelContentKey::contentRev(level);
}

int LevelFingerprint::objectCount(PlayLayer* layer) {
    if (!layer || !layer->m_objectLayer) return 0;
    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return 0;
    return static_cast<int>(children->count());
}

int LevelFingerprint::objectCount(GJGameLevel* level) {
    (void)level;
    return 0;
}

void LevelFingerprint::fillIdentity(
    LevelCacheIndex::Meta& meta,
    GJGameLevel* level,
    PlayLayer* layer,
    float startX,
    double tps,
    int bakeRadius
) {
    meta.schema = LevelCacheIndex::kSchema;
    if (level) {
        meta.fullKey = LevelContentKey::fullKey(level);
        meta.contentRev = contentRev(level);
    }
    meta.startXq = startXq(startX);
    meta.settingsFp = settingsFp();
    meta.objectCount = objectCount(layer);
    if (meta.objectCount <= 0) meta.objectCount = objectCount(level);
    meta.tps = tps > 0.0 ? tps : 240.0;
    meta.bakeRadius = bakeRadius;
    meta.gdVersion = "2.2081";
    if (auto* mod = Mod::get()) {
        meta.modVersion = mod->getVersion().toVString();
    }
    meta.stale = false;
}

bool LevelFingerprint::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(startXq(0.f) == 0, "xq0");
    expect(startXq(29.9f) == 0, "xq-under");
    expect(startXq(30.f) == 1, "xq-30");
    expect(startXq(90.f) == 3, "xq-90");
    expect(startXq(-0.1f) == -1, "xq-neg");
    expect(shardFolderName(0).empty(), "shard-root");
    expect(shardFolderName(3) == "sp_3", "shard-3");
    expect(shardFolderName(-2) == "sp_-2", "shard-neg");

    auto const a = settingsFp();
    auto const b = settingsFp();
    expect(!a.empty() && a == b, "settings-stable");
    expect(a.size() == 8, "settings-len");

    if (fails == 0) {
        report = "LevelFingerprint selfCheck OK";
        return true;
    }
    report = fmt::format("LevelFingerprint selfCheck {} fails: {}", fails, report);
    return false;
}
