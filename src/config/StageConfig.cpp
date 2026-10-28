#include "StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

static constexpr char const* PRESET_NAMES[] = { "Tight", "Normal", "Wide", "Max", "Off" };

struct PresetDef {
    bool enabled;
    int ticksAll;
    int ticksWave;
    int slack;
    int cap;
};

constexpr PresetDef PRESETS[] = {
    { true,  2,  2, 2, 0 }, // Tight
    { true,  4,  4, 2, 0 }, // Normal
    { true,  8,  8, 2, 0 }, // Wide
    { true, 16,  8, 2, 0 }, // Max
    { false, 0,  0, 2, 0 }, // Off
};

struct ModeIds {
    char const* enable;
    char const* overwrite;
    char const* ticks;
    char const* cap;
    char const* slack;
    char const* name;
};

constexpr ModeIds MODE_IDS[] = {
    { "cube-enable",   "cube-overwrite",   "cube-ticks",   "cube-cap",   "cube-slack",   "cube"   },
    { "ship-enable",   "ship-overwrite",   "ship-ticks",   "ship-cap",   "ship-slack",   "ship"   },
    { "ball-enable",   "ball-overwrite",   "ball-ticks",   "ball-cap",   "ball-slack",   "ball"   },
    { "ufo-enable",    "ufo-overwrite",    "ufo-ticks",    "ufo-cap",    "ufo-slack",    "ufo"    },
    { "wave-enable",   "wave-overwrite",   "wave-ticks",   "wave-cap",   "wave-slack",   "wave"   },
    { "robot-enable",  "robot-overwrite",  "robot-ticks",  "robot-cap",  "robot-slack",  "robot"  },
    { "spider-enable", "spider-overwrite", "spider-ticks", "spider-cap", "spider-slack", "spider" },
    { "swing-enable",  "swing-overwrite",  "swing-ticks",  "swing-cap",  "swing-slack",  "swing"  },
};

StageInfo g_cachedStage{};
std::array<ModeParams, StageConfig::MODE_COUNT> g_cachedModes{};
bool g_cachedDebug = true;
bool g_cachedSafeMode = false;
bool g_cachedShowOverlay = true;
bool g_cachedShowTrajectory = false;
bool g_cachedShowPauseBake = true;
bool g_cachedAssistPress = true;
bool g_cachedAssistRelease = true;
bool g_cachedAssistOrbs = true;
bool g_cachedSwallow = false;
bool g_cachedAutoTune = true;
AssistMode g_cachedAssistMode = AssistMode::Hybrid;
int g_cachedSimBudgetUs = 1500;
int g_cachedBakeRadius = 0;
int g_cachedCpsLimit = 0;
int g_cachedMinClickIntervalMs = 0;
int g_cachedSmartDepth = 1;
int g_cachedSmartCombos = 48;
int g_cachedSmartHorizon = 0;
std::string g_cachedAssistVerify = "off";
int g_cachedTickBudget = 4000;
bool g_cachedAssistOnlyPractice = false;
bool g_cachedAssistStartposOnly = false;
int g_cachedAssistPercentMin = 0;
int g_cachedAssistPercentMax = 100;
std::vector<StageConfig::PercentRange> g_cachedAssistDisableRanges;
bool g_cachedShowPauseAssist = true;
std::string g_cachedSaveFeedback = "off";
StageConfig::ColorRgb g_cachedSaveIconColor{};
int g_cachedSlack = 2;
int g_cachedLookAhead = 0;
bool g_cacheInitialized = false;
bool g_applying = false;
std::string g_lastSyncedPreset;

static int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

static int readInt(Mod* mod, char const* id, int fallback) {
    try {
        return static_cast<int>(mod->getSettingValue<int64_t>(id));
    }
    catch (...) {
        return fallback;
    }
}

static bool readBool(Mod* mod, char const* id, bool fallback) {
    try {
        return mod->getSettingValue<bool>(id);
    }
    catch (...) {
        return fallback;
    }
}

static float clampPct(float v) {
    if (!std::isfinite(v)) return 0.f;
    return std::max(0.f, std::min(100.f, v));
}

static float readFloat(Mod* mod, char const* id, float fallback) {
    try {
        return static_cast<float>(mod->getSettingValue<double>(id));
    }
    catch (...) {
        return fallback;
    }
}

static float round2(float v) {
    return std::round(v * 100.f) / 100.f;
}

static void loadAssistOffSlots(Mod* mod, std::vector<StageConfig::PercentRange>& out) {
    out.clear();
    static char const* fromIds[] = {
        "assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
        "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"
    };
    static char const* toIds[] = {
        "assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
        "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"
    };
    static_assert(sizeof(fromIds) / sizeof(fromIds[0]) == StageConfig::ASSIST_OFF_SLOTS);
    for (int i = 0; i < StageConfig::ASSIST_OFF_SLOTS; ++i) {
        float a = readFloat(mod, fromIds[i], -1.f);
        float b = readFloat(mod, toIds[i], -1.f);
        // -1 = unused slot
        if (a < 0.f || b < 0.f) continue;
        a = round2(clampPct(a));
        b = round2(clampPct(b));
        if (a > b) std::swap(a, b);
        out.push_back({ a, b });
    }
}

char const* StageConfig::presetName(int index) {
    if (index < 0 || index >= STAGE_COUNT) return "Off";
    return PRESET_NAMES[index];
}

int StageConfig::presetIndexByName(std::string const& name) {
    for (int i = 0; i < STAGE_COUNT; ++i) {
        if (name == PRESET_NAMES[i]) return i;
    }
    return 0;
}

void StageConfig::refreshCache() {
    auto* mod = Mod::get();
    g_cachedStage.index = clampInt(static_cast<int>(mod->getSettingValue<int64_t>("stage-index")), 0, STAGE_COUNT - 1);
    g_cachedStage.enabled = mod->getSettingValue<bool>("enabled");
    g_cachedStage.ticksAll = clampInt(static_cast<int>(mod->getSettingValue<int64_t>("ticks-all")), 0, 32);
    g_cachedStage.ticksWave = clampInt(static_cast<int>(mod->getSettingValue<int64_t>("ticks-wave")), 0, 32);

    g_cachedDebug = mod->getSettingValue<bool>("debug-logging");
    g_cachedSafeMode = mod->getSettingValue<bool>("safe-mode");
    g_cachedShowOverlay = mod->getSettingValue<bool>("show-overlay");
    g_cachedShowTrajectory = readBool(mod, "show-trajectory", false);
    g_cachedShowPauseBake = readBool(mod, "show-pause-bake", true);
    g_cachedAssistPress = readBool(mod, "assist-press", true);
    g_cachedAssistRelease = readBool(mod, "assist-release", true);
    g_cachedAssistOrbs = readBool(mod, "assist-orbs", true);
    g_cachedSwallow = readBool(mod, "swallow-hopeless", false);
    g_cachedAutoTune = readBool(mod, "auto-tune", true);
    g_cachedSimBudgetUs = clampInt(readInt(mod, "sim-budget-us", 1500), 0, 10000);
    g_cachedBakeRadius = clampInt(readInt(mod, "bake-radius", 0), 0, 128);
    g_cachedCpsLimit = clampInt(readInt(mod, "cps-limit", 0), 0, 40);
    g_cachedMinClickIntervalMs = clampInt(readInt(mod, "min-click-interval-ms", 0), 0, 500);
    g_cachedSmartDepth = clampInt(readInt(mod, "smart-depth", 1), 1, 3);
    g_cachedSmartCombos = clampInt(readInt(mod, "smart-combos", 48), 8, 256);
    g_cachedSmartHorizon = clampInt(readInt(mod, "smart-horizon", 0), 0, 320);
    g_cachedTickBudget = clampInt(readInt(mod, "tick-budget", 4000), 500, 20000);
    g_cachedAssistOnlyPractice = readBool(mod, "assist-only-practice", false);
    g_cachedAssistStartposOnly = readBool(mod, "assist-startpos-only", false);
    g_cachedAssistPercentMin = clampInt(readInt(mod, "assist-percent-min", 0), 0, 100);
    g_cachedAssistPercentMax = clampInt(readInt(mod, "assist-percent-max", 100), 0, 100);
    if (g_cachedAssistPercentMin > g_cachedAssistPercentMax) {
        std::swap(g_cachedAssistPercentMin, g_cachedAssistPercentMax);
    }
    loadAssistOffSlots(mod, g_cachedAssistDisableRanges);
    g_cachedShowPauseAssist = readBool(mod, "show-pause-assist", true);
    try {
        g_cachedSaveFeedback = mod->getSettingValue<std::string>("save-feedback");
        if (g_cachedSaveFeedback != "icon"
            && g_cachedSaveFeedback != "sound"
            && g_cachedSaveFeedback != "both") {
            g_cachedSaveFeedback = "off";
        }
    }
    catch (...) {
        g_cachedSaveFeedback = "off";
    }
    try {
        auto const c = mod->getSettingValue<cocos2d::ccColor3B>("save-icon-color");
        g_cachedSaveIconColor = { c.r, c.g, c.b };
    }
    catch (...) {
        g_cachedSaveIconColor = {};
    }
    try {
        g_cachedAssistVerify = mod->getSettingValue<std::string>("assist-verify");
        if (g_cachedAssistVerify != "ab") g_cachedAssistVerify = "off";
    }
    catch (...) {
        g_cachedAssistVerify = "off";
    }
    g_cachedSlack = clampInt(readInt(mod, "death-slack", 2), 0, 16);
    g_cachedLookAhead = clampInt(readInt(mod, "look-ahead-cap", 0), 0, 240);
    try {
        auto modeStr = mod->getSettingValue<std::string>("assist-mode");
        if (modeStr == "Live") g_cachedAssistMode = AssistMode::Live;
        else if (modeStr == "Replay") g_cachedAssistMode = AssistMode::Replay;
        else if (modeStr == "BruteForce") g_cachedAssistMode = AssistMode::BruteForce;
        else g_cachedAssistMode = AssistMode::Hybrid;
    }
    catch (...) {
        g_cachedAssistMode = AssistMode::Hybrid;
    }

    for (int i = 0; i < MODE_COUNT; ++i) {
        auto const& ids = MODE_IDS[i];
        ModeParams p;
        p.enabled = readBool(mod, ids.enable, true);
        p.overwrite = readBool(mod, ids.overwrite, false);
        int const fallbackTicks = (i == static_cast<int>(GameModeKind::Wave))
            ? g_cachedStage.ticksWave
            : g_cachedStage.ticksAll;
        if (p.overwrite) {
            p.ticks = clampInt(readInt(mod, ids.ticks, fallbackTicks), 0, 32);
            p.cap = clampInt(readInt(mod, ids.cap, 0), 0, 240);
            int slack = readInt(mod, ids.slack, -1);
            p.slack = slack < 0 ? g_cachedSlack : clampInt(slack, 0, 16);
        }
        else {
            p.ticks = fallbackTicks;
            p.cap = g_cachedLookAhead;
            p.slack = g_cachedSlack;
        }
        g_cachedModes[static_cast<size_t>(i)] = p;
    }

    if (g_lastSyncedPreset.empty()) {
        try {
            g_lastSyncedPreset = mod->getSettingValue<std::string>("preset");
        }
        catch (...) {
            g_lastSyncedPreset = PRESET_NAMES[g_cachedStage.index];
        }
    }

    g_cacheInitialized = true;
}

void ensureCache() {
    if (!g_cacheInitialized) StageConfig::refreshCache();
}

void StageConfig::notePresetSynced(std::string const& name) {
    g_lastSyncedPreset = name;
}

std::string StageConfig::lastSyncedPreset() {
    ensureCache();
    return g_lastSyncedPreset;
}

StageInfo StageConfig::preset(int index) {
    index = clampInt(index, 0, STAGE_COUNT - 1);
    auto const& def = PRESETS[index];
    StageInfo info;
    info.index = index;
    info.enabled = def.enabled;
    info.ticksAll = def.ticksAll;
    info.ticksWave = def.ticksWave;
    return info;
}

void StageConfig::applyPreset(int index) {
    index = clampInt(index, 0, STAGE_COUNT - 1);
    auto const info = preset(index);
    auto const& def = PRESETS[index];

    g_applying = true;
    g_cachedStage = info;
    g_cachedSlack = def.slack;
    g_cachedLookAhead = def.cap;
    g_cacheInitialized = true;

    auto* mod = Mod::get();
    mod->setSettingValue<int64_t>("stage-index", info.index);
    mod->setSettingValue<std::string>("preset", std::string(PRESET_NAMES[index]));
    mod->setSettingValue<bool>("enabled", info.enabled);
    mod->setSettingValue<int64_t>("ticks-all", info.ticksAll);
    mod->setSettingValue<int64_t>("ticks-wave", info.ticksWave);
    mod->setSettingValue<int64_t>("death-slack", def.slack);
    mod->setSettingValue<int64_t>("look-ahead-cap", def.cap);
    g_lastSyncedPreset = PRESET_NAMES[index];
    g_applying = false;

    refreshCache();
}

void StageConfig::applyPresetByName(std::string const& name) {
    applyPreset(presetIndexByName(name));
}

StageInfo StageConfig::cycleNext() {
    ensureCache();
    int next = g_cachedStage.index + 1;
    if (next >= STAGE_COUNT) next = 0;
    applyPreset(next);
    return g_cachedStage;
}

void StageConfig::toggleDebugLogging() {
    ensureCache();
    g_cachedDebug = !g_cachedDebug;
    Mod::get()->setSettingValue<bool>("debug-logging", g_cachedDebug);
}

void StageConfig::toggleAutoTune() {
    ensureCache();
    g_cachedAutoTune = !g_cachedAutoTune;
    Mod::get()->setSettingValue<bool>("auto-tune", g_cachedAutoTune);
}

char const* StageConfig::assistModeName(AssistMode mode) {
    switch (mode) {
        case AssistMode::Live: return "Live";
        case AssistMode::Replay: return "Replay";
        case AssistMode::Hybrid: return "Hybrid";
        case AssistMode::BruteForce: return "BruteForce";
    }
    return "Hybrid";
}

char const* StageConfig::assistModeHint(AssistMode mode) {
    switch (mode) {
        case AssistMode::Live: return "live plan each click";
        case AssistMode::Replay: return "bake windows only";
        case AssistMode::Hybrid: return "bake if near, else live";
        case AssistMode::BruteForce: return "pre-baked safe clicks (no replay)";
    }
    return "";
}

void StageConfig::setAssistMode(AssistMode mode) {
    ensureCache();
    g_cachedAssistMode = mode;
    Mod::get()->setSettingValue<std::string>("assist-mode", std::string(assistModeName(mode)));
}

void StageConfig::cycleAssistMode() {
    ensureCache();
    int next = (static_cast<int>(g_cachedAssistMode) + 1) % 4;
    setAssistMode(static_cast<AssistMode>(next));
}

void StageConfig::toggleShowTrajectory() {
    ensureCache();
    g_cachedShowTrajectory = !g_cachedShowTrajectory;
    Mod::get()->setSettingValue<bool>("show-trajectory", g_cachedShowTrajectory);
}

bool StageConfig::isApplying() { return g_applying; }

StageInfo StageConfig::fromSettings() {
    ensureCache();
    return g_cachedStage;
}

bool StageConfig::isEnabled() { ensureCache(); return g_cachedStage.enabled; }
bool StageConfig::debugLogging() { ensureCache(); return g_cachedDebug; }
bool StageConfig::safeMode() { ensureCache(); return g_cachedSafeMode; }
bool StageConfig::showOverlay() { ensureCache(); return g_cachedShowOverlay; }
bool StageConfig::showTrajectory() { ensureCache(); return g_cachedShowTrajectory; }
bool StageConfig::showPauseBake() { ensureCache(); return g_cachedShowPauseBake; }
bool StageConfig::assistPress() { ensureCache(); return g_cachedAssistPress; }
bool StageConfig::assistRelease() { ensureCache(); return g_cachedAssistRelease; }
bool StageConfig::assistOrbs() { ensureCache(); return g_cachedAssistOrbs; }
bool StageConfig::swallowHopeless() { ensureCache(); return g_cachedSwallow; }
bool StageConfig::autoTune() { ensureCache(); return g_cachedAutoTune; }
AssistMode StageConfig::assistMode() { ensureCache(); return g_cachedAssistMode; }
int StageConfig::simBudgetUs() { ensureCache(); return g_cachedSimBudgetUs; }
int StageConfig::bakeRadius() { ensureCache(); return g_cachedBakeRadius; }
int StageConfig::cpsLimit() { ensureCache(); return g_cachedCpsLimit; }
int StageConfig::minClickIntervalMs() { ensureCache(); return g_cachedMinClickIntervalMs; }
int StageConfig::smartDepth() { ensureCache(); return g_cachedSmartDepth; }
int StageConfig::smartCombos() { ensureCache(); return g_cachedSmartCombos; }
int StageConfig::smartHorizon() { ensureCache(); return g_cachedSmartHorizon; }
char const* StageConfig::assistVerifyMode() {
    ensureCache();
    return g_cachedAssistVerify.c_str();
}
bool StageConfig::assistVerifyAb() {
    ensureCache();
    return g_cachedAssistVerify == "ab";
}
int StageConfig::tickBudgetPerPress() { ensureCache(); return g_cachedTickBudget; }
bool StageConfig::assistOnlyPractice() { ensureCache(); return g_cachedAssistOnlyPractice; }
bool StageConfig::assistStartposOnly() { ensureCache(); return g_cachedAssistStartposOnly; }
int StageConfig::assistPercentMin() { ensureCache(); return g_cachedAssistPercentMin; }
int StageConfig::assistPercentMax() { ensureCache(); return g_cachedAssistPercentMax; }
std::vector<StageConfig::PercentRange> const& StageConfig::assistDisableRanges() {
    ensureCache();
    return g_cachedAssistDisableRanges;
}
bool StageConfig::isInAssistDisableRange(float percent) {
    ensureCache();
    float const p = clampPct(percent);
    for (auto const& r : g_cachedAssistDisableRanges) {
        if (p + 0.0001f >= r.from && p - 0.0001f <= r.to) return true;
    }
    return false;
}
bool StageConfig::showPauseAssist() { ensureCache(); return g_cachedShowPauseAssist; }
char const* StageConfig::saveFeedbackMode() { ensureCache(); return g_cachedSaveFeedback.c_str(); }
bool StageConfig::saveFeedbackIcon() {
    ensureCache();
    return g_cachedSaveFeedback == "icon" || g_cachedSaveFeedback == "both";
}
bool StageConfig::saveFeedbackSound() {
    ensureCache();
    return g_cachedSaveFeedback == "sound" || g_cachedSaveFeedback == "both";
}
StageConfig::ColorRgb StageConfig::saveFeedbackColor() {
    ensureCache();
    return g_cachedSaveIconColor;
}
int StageConfig::ticksAll() { ensureCache(); return g_cachedStage.ticksAll; }
int StageConfig::ticksWave() { ensureCache(); return g_cachedStage.ticksWave; }
int StageConfig::deathSlack() { ensureCache(); return g_cachedSlack; }
int StageConfig::lookAheadCap() { ensureCache(); return g_cachedLookAhead; }
int StageConfig::stageIndex() { ensureCache(); return g_cachedStage.index; }

GameModeKind StageConfig::kindOf(PlayerObject* player) {
    if (!player) return GameModeKind::Cube;
    if (player->m_isDart) return GameModeKind::Wave;
    if (player->m_isShip) return GameModeKind::Ship;
    if (player->m_isBall) return GameModeKind::Ball;
    if (player->m_isBird) return GameModeKind::Ufo;
    if (player->m_isRobot) return GameModeKind::Robot;
    if (player->m_isSpider) return GameModeKind::Spider;
    if (player->m_isSwing) return GameModeKind::Swing;
    return GameModeKind::Cube;
}

char const* StageConfig::kindName(GameModeKind kind) {
    int const i = static_cast<int>(kind);
    if (i < 0 || i >= MODE_COUNT) return "cube";
    return MODE_IDS[i].name;
}

ModeParams StageConfig::paramsFor(GameModeKind kind) {
    ensureCache();
    int const i = static_cast<int>(kind);
    if (i < 0 || i >= MODE_COUNT) return g_cachedModes[0];
    return g_cachedModes[static_cast<size_t>(i)];
}

ModeParams StageConfig::paramsFor(PlayerObject* player) {
    return paramsFor(kindOf(player));
}

std::string StageConfig::stageLabel(StageInfo const& info) {
    if (!info.enabled || info.index >= STAGE_COUNT - 1) return "OFF";
    return fmt::format("{} ({}A/{}W)", presetName(info.index), info.ticksAll, info.ticksWave);
}

