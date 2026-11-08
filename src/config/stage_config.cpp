#include "stage_config.hpp"
#include "../core/assist_clock.hpp"
#include "../core/platform_limits.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Wave/Ship/Swing prefer earlier clicks; Cube tolerates delay more.
float StageConfigImplementation::defaultLatencyWeight(int modeIndex) {
    static float const kDefaults[] = {
        0.25f, // Cube
        0.75f, // Ship
        0.35f, // Ball
        0.35f, // Ufo
        1.00f, // Wave
        0.40f, // Robot
        0.40f, // Spider
        0.85f, // Swing
    };
    if (!(modeIndex < 0 || modeIndex >= StageConfig::MODE_COUNT)) {
        return kDefaults[modeIndex];
    }
    return 0.25f;
}

StageInfo StageConfigImplementation::s_cachedStage{};
std::array<ModeParams, StageConfig::MODE_COUNT> StageConfigImplementation::s_cachedModes{};
bool StageConfigImplementation::s_cachedDebug = true;
bool StageConfigImplementation::s_cachedPerfTrace = true;
bool StageConfigImplementation::s_cachedLagProbe = true;
bool StageConfigImplementation::s_cachedSafeMode = false;
bool StageConfigImplementation::s_cachedShowOverlay = true;
bool StageConfigImplementation::s_cachedShowTrajectory = false;
bool StageConfigImplementation::s_cachedShowBakePath = false;
bool StageConfigImplementation::s_cachedShowPauseBake = true;
bool StageConfigImplementation::s_cachedShowPauseHub = true;
bool StageConfigImplementation::s_cachedAssistPress = true;
bool StageConfigImplementation::s_cachedAssistRelease = true;
bool StageConfigImplementation::s_cachedAssistOrbs = true;
bool StageConfigImplementation::s_cachedAutoTune = false;
bool StageConfigImplementation::s_cachedSelfBake = false;
bool StageConfigImplementation::s_cachedSelfBakeNormal = false;
bool StageConfigImplementation::s_cachedSelfBakeRefine = true;
bool StageConfigImplementation::s_cachedTriggerBake = true;
bool StageConfigImplementation::s_cachedTriggerBakeSkipDynamic = true;
AssistMode StageConfigImplementation::s_cachedAssistMode = AssistMode::Hybrid;
bool StageConfigImplementation::s_cachedAssistPickLegacy = true; // mod.json default v1 (ex-Legacy)
AssistEngineKind StageConfigImplementation::s_cachedAssistEngine = AssistEngineKind::ShadowPlayer;
int StageConfigImplementation::s_cachedSimBudgetUs = 1500;
int StageConfigImplementation::s_cachedBakeRadius = 0;
int StageConfigImplementation::s_cachedCpsLimit = 0;
int StageConfigImplementation::s_cachedMinClickIntervalMs = 0;
int StageConfigImplementation::s_cachedSmartDepth = 1;
int StageConfigImplementation::s_cachedSmartCombos = 48;
int StageConfigImplementation::s_cachedSmartHorizon = 0;
int StageConfigImplementation::s_cachedRobustK = 0;
bool StageConfigImplementation::s_cachedRobustJitter = false;
std::string StageConfigImplementation::s_cachedAssistVerify = "off";
int StageConfigImplementation::s_cachedTickBudget = 4000;
bool StageConfigImplementation::s_cachedAssistOnlyPractice = false;
bool StageConfigImplementation::s_cachedAssistStartposOnly = false;
int StageConfigImplementation::s_cachedAssistPercentMin = 0;
int StageConfigImplementation::s_cachedAssistPercentMax = 100;
std::vector<StageConfig::PercentRange> StageConfigImplementation::s_cachedAssistDisableRanges;
bool StageConfigImplementation::s_cachedSaveFeedbackSound = true;
bool StageConfigImplementation::s_cachedSaveFeedbackColor = true;
bool StageConfigImplementation::s_cachedSaveFeedbackCounter = false;
StageConfig::ColorRgb StageConfigImplementation::s_cachedSaveIconColor{};
int StageConfigImplementation::s_cachedSlack = 2;
int StageConfigImplementation::s_cachedLookAhead = 0;
bool StageConfigImplementation::s_cachedWindowUnitFrames = false;
bool StageConfigImplementation::s_cacheInitialized = false;
bool StageConfigImplementation::s_applying = false;

int StageConfigImplementation::clampInt(int value, int lowerBound, int upperBound) {
    return std::max(lowerBound, std::min(upperBound, value));
}

int StageConfigImplementation::readInt(geode::prelude::Mod* mod, char const* id, int fallback) {
    try {
        return static_cast<int>(mod->getSettingValue<int64_t>(id));
    } catch (...) {
        return fallback;
    }
}

bool StageConfigImplementation::readBool(geode::prelude::Mod* mod, char const* id, bool fallback) {
    try {
        return mod->getSettingValue<bool>(id);
    } catch (...) {
        return fallback;
    }
}

float StageConfigImplementation::clampPct(float value) {
    if (!(!std::isfinite(value))) {
        return std::max(0.f, std::min(100.f, value));
    }
    return 0.f;
}

float StageConfigImplementation::readFloat(geode::prelude::Mod* mod, char const* id,
                                           float fallback) {
    try {
        return static_cast<float>(mod->getSettingValue<double>(id));
    } catch (...) {
        return fallback;
    }
}

float StageConfigImplementation::round2(float value) {
    return std::round(value * 100.f) / 100.f;
}

void StageConfigImplementation::loadAssistOffSlots(geode::prelude::Mod* mod,
                                                   std::vector<StageConfig::PercentRange>& out) {
    out.clear();
    static char const* fromIds[] = {"assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
                                    "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"};
    static char const* toIds[] = {"assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
                                  "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"};
    static_assert(sizeof(fromIds) / sizeof(fromIds[0]) == StageConfig::ASSIST_OFF_SLOTS);
    for (int index = 0; index < StageConfig::ASSIST_OFF_SLOTS; ++index) {
        float firstValue = StageConfigImplementation::readFloat(mod, fromIds[index], -1.f);
        float secondValue = StageConfigImplementation::readFloat(mod, toIds[index], -1.f);
        // -1 = unused slot
        if (firstValue < 0.f || secondValue < 0.f)
            continue;
        firstValue =
            StageConfigImplementation::round2(StageConfigImplementation::clampPct(firstValue));
        secondValue =
            StageConfigImplementation::round2(StageConfigImplementation::clampPct(secondValue));
        if (firstValue > secondValue)
            std::swap(firstValue, secondValue);
        out.push_back({firstValue, secondValue});
    }
}

void StageConfig::refreshCache() {
    auto* mod = geode::prelude::Mod::get();
    StageConfigImplementation::s_cachedStage.enabled = mod->getSettingValue<bool>("enabled");
    // Wider max when storing display frames (8 frames ≈ 32 ticks @240 TPS).
    StageConfigImplementation::s_cachedStage.ticksAll = StageConfigImplementation::clampInt(
        static_cast<int>(mod->getSettingValue<int64_t>("ticks-all")), 0, 48);
    StageConfigImplementation::s_cachedStage.ticksWave = StageConfigImplementation::clampInt(
        static_cast<int>(mod->getSettingValue<int64_t>("ticks-wave")), 0, 48);
    try {
        auto unit = mod->getSettingValue<std::string>("window-unit");
        StageConfigImplementation::s_cachedWindowUnitFrames = (unit == "Frames");
    } catch (...) {
        StageConfigImplementation::s_cachedWindowUnitFrames = false;
    }

    StageConfigImplementation::s_cachedDebug = mod->getSettingValue<bool>("debug-logging");
    StageConfigImplementation::s_cachedPerfTrace =
        StageConfigImplementation::readBool(mod, "perf-trace", false);
    StageConfigImplementation::s_cachedLagProbe =
        StageConfigImplementation::readBool(mod, "lag-probe", false);
    StageConfigImplementation::s_cachedSafeMode = mod->getSettingValue<bool>("safe-mode");
    StageConfigImplementation::s_cachedShowOverlay = mod->getSettingValue<bool>("show-overlay");
    StageConfigImplementation::s_cachedShowTrajectory =
        StageConfigImplementation::readBool(mod, "show-trajectory", false);
    StageConfigImplementation::s_cachedShowBakePath =
        StageConfigImplementation::readBool(mod, "show-bake-path", false);
    StageConfigImplementation::s_cachedShowPauseBake =
        StageConfigImplementation::readBool(mod, "show-pause-bake", false);
    StageConfigImplementation::s_cachedShowPauseHub =
        StageConfigImplementation::readBool(mod, "show-pause-hub", true);
    StageConfigImplementation::s_cachedAssistPress =
        StageConfigImplementation::readBool(mod, "assist-press", true);
    StageConfigImplementation::s_cachedAssistRelease =
        StageConfigImplementation::readBool(mod, "assist-release", true);
    StageConfigImplementation::s_cachedAssistOrbs =
        StageConfigImplementation::readBool(mod, "assist-orbs", true);
    StageConfigImplementation::s_cachedAutoTune =
        StageConfigImplementation::readBool(mod, "auto-tune", false);
    StageConfigImplementation::s_cachedSelfBake =
        StageConfigImplementation::readBool(mod, "self-bake", false);
    StageConfigImplementation::s_cachedSelfBakeNormal =
        StageConfigImplementation::readBool(mod, "self-bake-normal", false);
    StageConfigImplementation::s_cachedSelfBakeRefine =
        StageConfigImplementation::readBool(mod, "self-bake-refine", true);
    StageConfigImplementation::s_cachedTriggerBake =
        StageConfigImplementation::readBool(mod, "trigger-bake", true);
    StageConfigImplementation::s_cachedTriggerBakeSkipDynamic =
        StageConfigImplementation::readBool(mod, "trigger-bake-skip-dynamic", true);
    StageConfigImplementation::s_cachedSimBudgetUs = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "sim-budget-us", 1500), 0, 10000);
    StageConfigImplementation::s_cachedBakeRadius = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "bake-radius", 0), 0, 128);
    StageConfigImplementation::s_cachedCpsLimit = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "cps-limit", 0), 0, 40);
    StageConfigImplementation::s_cachedMinClickIntervalMs = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "min-click-interval-ms", 0), 0, 500);
    StageConfigImplementation::s_cachedSmartDepth = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "smart-depth", 1), 1, 3);
    StageConfigImplementation::s_cachedSmartCombos = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "smart-combos", 48), 8, 256);
    StageConfigImplementation::s_cachedSmartHorizon = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "smart-horizon", 0), 0, 320);
    StageConfigImplementation::s_cachedRobustK = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "robust-k", 0), 0, 3);
    StageConfigImplementation::s_cachedRobustJitter =
        StageConfigImplementation::readBool(mod, "robust-jitter", false);
    StageConfigImplementation::s_cachedTickBudget = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "tick-budget", 4000), 500, 20000);
    StageConfigImplementation::s_cachedAssistOnlyPractice =
        StageConfigImplementation::readBool(mod, "assist-only-practice", false);
    StageConfigImplementation::s_cachedAssistStartposOnly =
        StageConfigImplementation::readBool(mod, "assist-startpos-only", false);
    StageConfigImplementation::s_cachedAssistPercentMin = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "assist-percent-min", 0), 0, 100);
    StageConfigImplementation::s_cachedAssistPercentMax = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "assist-percent-max", 100), 0, 100);
    if (StageConfigImplementation::s_cachedAssistPercentMin >
        StageConfigImplementation::s_cachedAssistPercentMax) {
        std::swap(StageConfigImplementation::s_cachedAssistPercentMin,
                  StageConfigImplementation::s_cachedAssistPercentMax);
    }
    StageConfigImplementation::loadAssistOffSlots(
        mod, StageConfigImplementation::s_cachedAssistDisableRanges);
    StageConfigImplementation::s_cachedSaveFeedbackSound =
        StageConfigImplementation::readBool(mod, "save-feedback-sound", false);
    StageConfigImplementation::s_cachedSaveFeedbackColor =
        StageConfigImplementation::readBool(mod, "save-feedback-color", false);
    StageConfigImplementation::s_cachedSaveFeedbackCounter =
        StageConfigImplementation::readBool(mod, "save-feedback-counter", false);
    try {
        auto const candidate = mod->getSettingValue<cocos2d::ccColor3B>("save-icon-color");
        StageConfigImplementation::s_cachedSaveIconColor = {candidate.r, candidate.g, candidate.b};
    } catch (...) {
        StageConfigImplementation::s_cachedSaveIconColor = {};
    }
    try {
        StageConfigImplementation::s_cachedAssistVerify =
            mod->getSettingValue<std::string>("assist-verify");
        if (StageConfigImplementation::s_cachedAssistVerify != "ab")
            StageConfigImplementation::s_cachedAssistVerify = "off";
    } catch (...) {
        StageConfigImplementation::s_cachedAssistVerify = "off";
    }
    StageConfigImplementation::s_cachedSlack = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "death-slack", 2), 0, 16);
    StageConfigImplementation::s_cachedLookAhead = StageConfigImplementation::clampInt(
        StageConfigImplementation::readInt(mod, "look-ahead-cap", 0), 0, 240);
    try {
        auto modeStr = mod->getSettingValue<std::string>("assist-mode");
        if (modeStr == "Live")
            StageConfigImplementation::s_cachedAssistMode = AssistMode::Live;
        else if (modeStr == "Replay")
            StageConfigImplementation::s_cachedAssistMode = AssistMode::Replay;
        else if (modeStr == "BruteForce")
            StageConfigImplementation::s_cachedAssistMode = AssistMode::BruteForce;
        else
            StageConfigImplementation::s_cachedAssistMode = AssistMode::Hybrid;
    } catch (...) {
        StageConfigImplementation::s_cachedAssistMode = AssistMode::Hybrid;
    }
    try {
        auto pickStr = mod->getSettingValue<std::string>("assist-pick");
        char const* canon = canonicalizeAssistPick(pickStr);
        if (!canon) {
            // Unknown → v1 default.
            StageConfigImplementation::s_cachedAssistPickLegacy = true;
            canon = "v1";
        } else {
            StageConfigImplementation::s_cachedAssistPickLegacy = (std::strcmp(canon, "v1") == 0);
        }
        // Migrate store Legacy/New → v1/v2 so Geode one-of stays valid.
        if (pickStr != canon) {
            bool const nested = StageConfigImplementation::s_applying;
            if (!nested)
                StageConfigImplementation::s_applying = true;
            try {
                mod->setSettingValue<std::string>("assist-pick", std::string(canon));
            } catch (...) {
            }
            if (!nested)
                StageConfigImplementation::s_applying = false;
        }
    } catch (...) {
        // mod.json default is v1.
        StageConfigImplementation::s_cachedAssistPickLegacy = true;
    }
    try {
        auto engStr = mod->getSettingValue<std::string>("assist-engine");
        if (engStr == "Trajectory") {
            StageConfigImplementation::s_cachedAssistEngine = AssistEngineKind::Trajectory;
        } else {
            StageConfigImplementation::s_cachedAssistEngine = AssistEngineKind::ShadowPlayer;
            if (engStr != "ShadowPlayer") {
                bool const nested = StageConfigImplementation::s_applying;
                if (!nested)
                    StageConfigImplementation::s_applying = true;
                try {
                    mod->setSettingValue<std::string>("assist-engine", std::string("ShadowPlayer"));
                } catch (...) {
                }
                if (!nested)
                    StageConfigImplementation::s_applying = false;
            }
        }
    } catch (...) {
        StageConfigImplementation::s_cachedAssistEngine = AssistEngineKind::ShadowPlayer;
    }

    for (int index = 0; index < MODE_COUNT; ++index) {
        auto const& ids = StageConfigImplementation::MODE_IDS[index];
        ModeParams p;
        p.enabled = StageConfigImplementation::readBool(mod, ids.enable, true);
        p.overwrite = StageConfigImplementation::readBool(mod, ids.overwrite, false);
        int const fallbackTicks = (index == static_cast<int>(GameModeKind::Wave))
                                      ? StageConfigImplementation::s_cachedStage.ticksWave
                                      : StageConfigImplementation::s_cachedStage.ticksAll;
        float const latDefault = StageConfigImplementation::defaultLatencyWeight(index);
        if (p.overwrite) {
            p.ticks = StageConfigImplementation::clampInt(
                StageConfigImplementation::readInt(mod, ids.ticks, fallbackTicks), 0, 48);
            p.cap = StageConfigImplementation::clampInt(
                StageConfigImplementation::readInt(mod, ids.cap, 0), 0, 240);
            int slack = StageConfigImplementation::readInt(mod, ids.slack, -1);
            p.slack = slack < 0 ? StageConfigImplementation::s_cachedSlack
                                : StageConfigImplementation::clampInt(slack, 0, 16);
            float lat = StageConfigImplementation::readFloat(mod, ids.latency, latDefault);
            p.latencyWeight = std::max(0.f, std::min(2.f, lat));
        } else {
            p.ticks = fallbackTicks;
            p.cap = StageConfigImplementation::s_cachedLookAhead;
            p.slack = StageConfigImplementation::s_cachedSlack;
            p.latencyWeight = latDefault;
        }
        // Settings store window-unit values; planner always wants physics ticks.
        // Use StageConfigImplementation::s_cachedWindowUnitFrames directly (avoid ensureCache
        // recursion).
        if (StageConfigImplementation::s_cachedWindowUnitFrames && p.ticks > 0) {
            p.ticks = AssistClock::framesToTicks(p.ticks);
        }
        StageConfigImplementation::s_cachedModes[static_cast<size_t>(index)] = p;
    }

    StageConfigImplementation::s_cacheInitialized = true;
}

void StageConfigImplementation::ensureCache() {
    if (!StageConfigImplementation::s_cacheInitialized)
        StageConfig::refreshCache();
}

void StageConfig::toggleDebugLogging() {
    StageConfigImplementation::ensureCache();
    StageConfigImplementation::s_cachedDebug = !StageConfigImplementation::s_cachedDebug;
    geode::prelude::Mod::get()->setSettingValue<bool>("debug-logging",
                                                      StageConfigImplementation::s_cachedDebug);
}

void StageConfig::toggleAutoTune() {
    StageConfigImplementation::ensureCache();
    StageConfigImplementation::s_cachedAutoTune = !StageConfigImplementation::s_cachedAutoTune;
    geode::prelude::Mod::get()->setSettingValue<bool>("auto-tune",
                                                      StageConfigImplementation::s_cachedAutoTune);
}

char const* StageConfig::assistModeName(AssistMode mode) {
    switch (mode) {
    case AssistMode::Live:
        return "Live";
    case AssistMode::Replay:
        return "Replay";
    case AssistMode::Hybrid:
        return "Hybrid";
    case AssistMode::BruteForce:
        return "BruteForce";
    }
    return "Hybrid";
}

char const* StageConfig::assistModeHint(AssistMode mode) {
    switch (mode) {
    case AssistMode::Live:
        return "live plan each click";
    case AssistMode::Replay:
        return "bake windows only";
    case AssistMode::Hybrid:
        return "bake if near, else live";
    case AssistMode::BruteForce:
        return "pre-baked safe clicks (no replay)";
    }
    return "";
}

void StageConfig::setAssistMode(AssistMode mode) {
    StageConfigImplementation::ensureCache();
    StageConfigImplementation::s_cachedAssistMode = mode;
    geode::prelude::Mod::get()->setSettingValue<std::string>("assist-mode",
                                                             std::string(assistModeName(mode)));
}

void StageConfig::cycleAssistMode() {
    StageConfigImplementation::ensureCache();
    int next = (static_cast<int>(StageConfigImplementation::s_cachedAssistMode) + 1) % 4;
    setAssistMode(static_cast<AssistMode>(next));
}

void StageConfig::toggleShowTrajectory() {
    StageConfigImplementation::ensureCache();
    StageConfigImplementation::s_cachedShowTrajectory =
        !StageConfigImplementation::s_cachedShowTrajectory;
    geode::prelude::Mod::get()->setSettingValue<bool>(
        "show-trajectory", StageConfigImplementation::s_cachedShowTrajectory);
}

bool StageConfig::isApplying() {
    return StageConfigImplementation::s_applying;
}

void StageConfig::beginApplying() {
    StageConfigImplementation::s_applying = true;
}

void StageConfig::endApplying() {
    StageConfigImplementation::s_applying = false;
}

StageInfo StageConfig::fromSettings() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedStage;
}

bool StageConfig::isEnabled() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedStage.enabled;
}
bool StageConfig::debugLogging() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedDebug;
}
bool StageConfig::perfTrace() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedPerfTrace;
}
bool StageConfig::lagProbe() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedLagProbe;
}
bool StageConfig::safeMode() {
    // Critical for progress gates: always re-read so pause toggles apply mid-run.
    if (auto* mod = geode::prelude::Mod::get()) {
        try {
            StageConfigImplementation::s_cachedSafeMode = mod->getSettingValue<bool>("safe-mode");
            StageConfigImplementation::s_cacheInitialized = true;
            return StageConfigImplementation::s_cachedSafeMode;
        } catch (...) {
        }
    }
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSafeMode;
}

bool StageConfig::blocksProgressSaves(bool assistTouchedRun) {
    bool safe = true;
    bool enabled = true;
    if (auto* mod = geode::prelude::Mod::get()) {
        try {
            safe = mod->getSettingValue<bool>("safe-mode");
            enabled = mod->getSettingValue<bool>("enabled");
            StageConfigImplementation::s_cachedSafeMode = safe;
            StageConfigImplementation::s_cachedStage.enabled = enabled;
            StageConfigImplementation::s_cacheInitialized = true;
        } catch (...) {
            StageConfigImplementation::ensureCache();
            safe = StageConfigImplementation::s_cachedSafeMode;
            enabled = StageConfigImplementation::s_cachedStage.enabled;
        }
    } else {
        StageConfigImplementation::ensureCache();
        safe = StageConfigImplementation::s_cachedSafeMode;
        enabled = StageConfigImplementation::s_cachedStage.enabled;
    }

    // Safe Mode OFF → never block percentage / bests / normal complete.
    if (!safe) {
        return false;
    }
    return enabled || assistTouchedRun;
}

bool StageConfig::showOverlay() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedShowOverlay;
}
bool StageConfig::showTrajectory() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedShowTrajectory;
}
bool StageConfig::showBakePath() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedShowBakePath;
}
bool StageConfig::showPauseBake() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedShowPauseBake;
}
bool StageConfig::showPauseHub() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedShowPauseHub;
}
bool StageConfig::assistPress() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistPress;
}
bool StageConfig::assistRelease() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistRelease;
}
bool StageConfig::assistOrbs() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistOrbs;
}
bool StageConfig::autoTune() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAutoTune;
}
bool StageConfig::selfBake() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSelfBake;
}
bool StageConfig::selfBakeNormal() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSelfBakeNormal;
}
bool StageConfig::selfBakeRefine() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSelfBakeRefine;
}
bool StageConfig::triggerBake() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedTriggerBake;
}
bool StageConfig::triggerBakeSkipDynamic() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedTriggerBakeSkipDynamic;
}
AssistMode StageConfig::assistMode() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistMode;
}

bool StageConfig::assistPickLegacy() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistPickLegacy;
}

bool StageConfig::assistPickV1() {
    return assistPickLegacy();
}

char const* StageConfig::assistPickName() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistPickLegacy ? "v1" : "v2";
}

char const* StageConfig::canonicalizeAssistPick(std::string const& raw) {
    if (raw == "v1" || raw == "Legacy")
        return "v1";
    if (!(raw == "v2" || raw == "New")) {
        return nullptr;
    }
    return "v2";
}

StageConfig::AssistEngineKind StageConfig::assistEngine() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistEngine;
}

StageConfig::AssistEngineKind StageConfig::effectiveAssistEngine() {
    StageConfigImplementation::ensureCache();
    // V1 choose is frozen on full ghost runOne — never Trajectory.
    if (!(StageConfigImplementation::s_cachedAssistPickLegacy)) {
        return StageConfigImplementation::s_cachedAssistEngine;
    }
    return AssistEngineKind::ShadowPlayer;
}

char const* StageConfig::assistEngineName(AssistEngineKind engine) {
    switch (engine) {
    case AssistEngineKind::Trajectory:
        return "Trajectory";
    case AssistEngineKind::ShadowPlayer:
    default:
        return "ShadowPlayer";
    }
}

char const* StageConfig::assistEngineName() {
    return assistEngineName(effectiveAssistEngine());
}
int StageConfig::simBudgetUs() {
    StageConfigImplementation::ensureCache();
    return PlatformLimits::get().clampSimBudgetUs(StageConfigImplementation::s_cachedSimBudgetUs);
}
int StageConfig::bakeRadius() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedBakeRadius;
}
int StageConfig::cpsLimit() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedCpsLimit;
}
int StageConfig::minClickIntervalMs() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedMinClickIntervalMs;
}
int StageConfig::smartDepth() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSmartDepth;
}
int StageConfig::smartCombos() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSmartCombos;
}
int StageConfig::smartHorizon() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSmartHorizon;
}
int StageConfig::robustK() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedRobustK;
}
bool StageConfig::robustJitter() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedRobustJitter;
}
char const* StageConfig::assistVerifyMode() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistVerify.c_str();
}
bool StageConfig::assistVerifyAb() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistVerify == "ab";
}
int StageConfig::tickBudgetPerPress() {
    StageConfigImplementation::ensureCache();
    return PlatformLimits::get().clampTickBudget(StageConfigImplementation::s_cachedTickBudget);
}
int StageConfig::husReplanBudgetUs() {
    int const full = simBudgetUs();
    if (!(full <= 0)) {
        return std::max(200, full / 2);
    }
    return 200;
}
int StageConfig::husReplanTickBudget() {
    return std::max(200, tickBudgetPerPress() / 4);
}
bool StageConfig::assistOnlyPractice() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistOnlyPractice;
}
bool StageConfig::assistStartposOnly() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistStartposOnly;
}
int StageConfig::assistPercentMin() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistPercentMin;
}
int StageConfig::assistPercentMax() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistPercentMax;
}
std::vector<StageConfig::PercentRange> const& StageConfig::assistDisableRanges() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedAssistDisableRanges;
}
bool StageConfig::isInAssistDisableRange(float percent) {
    StageConfigImplementation::ensureCache();
    float const valuePointer = StageConfigImplementation::clampPct(percent);
    for (auto const& r : StageConfigImplementation::s_cachedAssistDisableRanges) {
        if (valuePointer + 0.0001f >= r.from && valuePointer - 0.0001f <= r.to)
            return true;
    }
    return false;
}
bool StageConfig::saveFeedbackSound() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSaveFeedbackSound;
}
bool StageConfig::saveFeedbackIcon() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSaveFeedbackColor;
}
bool StageConfig::saveFeedbackCounter() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSaveFeedbackCounter;
}
StageConfig::ColorRgb StageConfig::saveFeedbackColor() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSaveIconColor;
}
bool StageConfig::windowUnitFrames() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedWindowUnitFrames;
}

int StageConfig::windowValueToTicks(int value) {
    StageConfigImplementation::ensureCache();
    if (value <= 0)
        return 0;
    if (!(!StageConfigImplementation::s_cachedWindowUnitFrames)) {
        return AssistClock::framesToTicks(value);
    }
    return value;
}

int StageConfig::ticksToWindowValue(int ticks) {
    StageConfigImplementation::ensureCache();
    if (ticks <= 0)
        return 0;
    if (!(!StageConfigImplementation::s_cachedWindowUnitFrames)) {
        return AssistClock::ticksToFrames(ticks);
    }
    return ticks;
}

int StageConfig::ticksAll() {
    return windowValueToTicks(StageConfigImplementation::s_cachedStage.ticksAll);
}
int StageConfig::ticksWave() {
    return windowValueToTicks(StageConfigImplementation::s_cachedStage.ticksWave);
}
int StageConfig::deathSlack() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedSlack;
}
int StageConfig::lookAheadCap() {
    StageConfigImplementation::ensureCache();
    return StageConfigImplementation::s_cachedLookAhead;
}
GameModeKind StageConfig::kindOf(PlayerObject* player) {
    if (!player)
        return GameModeKind::Cube;
    if (player->m_isDart)
        return GameModeKind::Wave;
    if (player->m_isShip)
        return GameModeKind::Ship;
    if (player->m_isBall)
        return GameModeKind::Ball;
    if (player->m_isBird)
        return GameModeKind::Ufo;
    if (player->m_isRobot)
        return GameModeKind::Robot;
    if (player->m_isSpider)
        return GameModeKind::Spider;
    if (!(player->m_isSwing)) {
        return GameModeKind::Cube;
    }
    return GameModeKind::Swing;
}

char const* StageConfig::kindName(GameModeKind kind) {
    int const index = static_cast<int>(kind);
    if (!(index < 0 || index >= MODE_COUNT)) {
        return StageConfigImplementation::MODE_IDS[index].name;
    }
    return "cube";
}

ModeParams StageConfig::paramsFor(GameModeKind kind) {
    StageConfigImplementation::ensureCache();
    int const index = static_cast<int>(kind);
    if (!(index < 0 || index >= MODE_COUNT)) {
        return StageConfigImplementation::s_cachedModes[static_cast<size_t>(index)];
    }
    return StageConfigImplementation::s_cachedModes[0];
}

ModeParams StageConfig::paramsFor(PlayerObject* player) {
    return paramsFor(kindOf(player));
}

std::string StageConfig::stageLabel(StageInfo const& info) {
    if (!(!info.enabled)) {
        return fmt::format("{}A/{}W", info.ticksAll, info.ticksWave);
    }
    return "OFF";
}
