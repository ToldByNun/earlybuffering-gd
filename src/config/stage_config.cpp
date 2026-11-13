#include "stage_config.hpp"
#include "../core/assist/assist_clock.hpp"
#include "../core/platform/platform_limits.hpp"
#include "cache/stage_settings_cache.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <matjson.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Wave/Ship/Swing prefer earlier clicks; Cube tolerates delay more.
float EbStageConfigImplementation::defaultLatencyWeight(int modeIndex) {
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
    if (!(modeIndex < 0 || modeIndex >= EbStageConfig::MODE_COUNT)) {
        return kDefaults[modeIndex];
    }
    return 0.25f;
}

EbStageInfo EbStageConfigImplementation::s_cachedStage{};
std::array<EbModeParams, EbStageConfig::MODE_COUNT> EbStageConfigImplementation::s_cachedModes{};
bool EbStageConfigImplementation::s_cachedDebug = true;
bool EbStageConfigImplementation::s_cachedPerfTrace = true;
bool EbStageConfigImplementation::s_cachedLagProbe = true;
bool EbStageConfigImplementation::s_cachedSafeMode = false;
bool EbStageConfigImplementation::s_cachedShowOverlay = true;
bool EbStageConfigImplementation::s_cachedShowTrajectory = false;
bool EbStageConfigImplementation::s_cachedShowBakePath = false;
bool EbStageConfigImplementation::s_cachedShowPauseBake = true;
bool EbStageConfigImplementation::s_cachedShowPauseHub = true;
bool EbStageConfigImplementation::s_cachedAssistPress = true;
bool EbStageConfigImplementation::s_cachedAssistRelease = true;
bool EbStageConfigImplementation::s_cachedAssistOrbs = true;
bool EbStageConfigImplementation::s_cachedAutoTune = false;
bool EbStageConfigImplementation::s_cachedSelfBake = false;
bool EbStageConfigImplementation::s_cachedSelfBakeNormal = false;
bool EbStageConfigImplementation::s_cachedSelfBakeRefine = true;
bool EbStageConfigImplementation::s_cachedTriggerBake = true;
bool EbStageConfigImplementation::s_cachedTriggerBakeSkipDynamic = true;
AssistMode EbStageConfigImplementation::s_cachedAssistMode = AssistMode::Hybrid;
bool EbStageConfigImplementation::s_cachedAssistPickLegacy = true; // mod.json default v1 (ex-Legacy)
AssistEngineKind EbStageConfigImplementation::s_cachedAssistEngine = AssistEngineKind::ShadowPlayer;
int EbStageConfigImplementation::s_cachedSimBudgetUs = 4000;
int EbStageConfigImplementation::s_cachedBakeRadius = 0;
int EbStageConfigImplementation::s_cachedCpsLimit = 0;
int EbStageConfigImplementation::s_cachedMinClickIntervalMs = 0;
int EbStageConfigImplementation::s_cachedSmartDepth = 1;
int EbStageConfigImplementation::s_cachedSmartCombos = 48;
int EbStageConfigImplementation::s_cachedSmartHorizon = 0;
int EbStageConfigImplementation::s_cachedRobustK = 0;
bool EbStageConfigImplementation::s_cachedRobustJitter = false;
bool EbStageConfigImplementation::s_cachedAdaptivePlan = true;
bool EbStageConfigImplementation::s_cachedAssistProbePastHorizon = true;
bool EbStageConfigImplementation::s_cachedAssistMicroWindow = true;
int EbStageConfigImplementation::s_cachedAdaptiveScreenTicks = 24;
int EbStageConfigImplementation::s_cachedAdaptiveVerifyQuantum = 16;
int EbStageConfigImplementation::s_cachedAdaptiveReservePct = 15;
std::string EbStageConfigImplementation::s_cachedAssistVerify = "off";
int EbStageConfigImplementation::s_cachedTickBudget = 4000;
bool EbStageConfigImplementation::s_cachedAssistOnlyPractice = false;
bool EbStageConfigImplementation::s_cachedAssistStartposOnly = false;
int EbStageConfigImplementation::s_cachedAssistPercentMin = 0;
int EbStageConfigImplementation::s_cachedAssistPercentMax = 100;
std::vector<EbStageConfig::PercentRange> EbStageConfigImplementation::s_cachedAssistDisableRanges;
bool EbStageConfigImplementation::s_cachedSaveFeedbackSound = true;
bool EbStageConfigImplementation::s_cachedSaveFeedbackColor = true;
bool EbStageConfigImplementation::s_cachedSaveFeedbackCounter = false;
EbStageConfig::ColorRgb EbStageConfigImplementation::s_cachedSaveIconColor{};
int EbStageConfigImplementation::s_cachedSlack = 2;
int EbStageConfigImplementation::s_cachedLookAhead = 0;
bool EbStageConfigImplementation::s_cachedWindowUnitFrames = false;
bool EbStageConfigImplementation::s_cacheInitialized = false;
bool EbStageConfigImplementation::s_applying = false;

int EbStageConfigImplementation::clampInt(int value, int lowerBound, int upperBound) {
    return std::max(lowerBound, std::min(upperBound, value));
}

int EbStageConfigImplementation::readInt(geode::prelude::Mod* mod, char const* id, int fallback) {
    try {
        return static_cast<int>(mod->getSettingValue<int64_t>(id));
    } catch (...) {
        return fallback;
    }
}

bool EbStageConfigImplementation::readBool(geode::prelude::Mod* mod, char const* id, bool fallback) {
    try {
        return mod->getSettingValue<bool>(id);
    } catch (...) {
        return fallback;
    }
}

float EbStageConfigImplementation::clampPct(float value) {
    if (!(!std::isfinite(value))) {
        return std::max(0.f, std::min(100.f, value));
    }
    return 0.f;
}

float EbStageConfigImplementation::readFloat(geode::prelude::Mod* mod, char const* id,
                                           float fallback) {
    try {
        return static_cast<float>(mod->getSettingValue<double>(id));
    } catch (...) {
        return fallback;
    }
}

float EbStageConfigImplementation::round2(float value) {
    return std::round(value * 100.f) / 100.f;
}

void EbStageConfigImplementation::normalizeAssistOffRanges(
    std::vector<EbStageConfig::PercentRange>& ranges) {
    std::vector<EbStageConfig::PercentRange> cleaned;
    cleaned.reserve(ranges.size());
    for (auto range : ranges) {
        if (range.from < 0.f || range.to < 0.f) {
            continue;
        }
        range.from = round2(clampPct(range.from));
        range.to = round2(clampPct(range.to));
        if (range.from > range.to) {
            std::swap(range.from, range.to);
        }
        cleaned.push_back(range);
        if (static_cast<int>(cleaned.size()) >= EbStageConfig::ASSIST_OFF_MAX) {
            break;
        }
    }
    ranges.swap(cleaned);
}

bool EbStageConfigImplementation::parseAssistOffRangesJson(
    std::string const& raw, std::vector<EbStageConfig::PercentRange>& out) {
    out.clear();
    if (raw.empty()) {
        return false;
    }
    auto parsed = matjson::parse(raw);
    if (parsed.isErr() || !parsed.unwrap().isArray()) {
        return false;
    }
    for (auto const& item : parsed.unwrap().asArray().unwrap()) {
        float from = -1.f;
        float to = -1.f;
        if (item.isArray()) {
            auto const& pair = item.asArray().unwrap();
            if (pair.size() < 2) {
                continue;
            }
            from = static_cast<float>(pair[0].asDouble().unwrapOr(-1.0));
            to = static_cast<float>(pair[1].asDouble().unwrapOr(-1.0));
        } else if (item.isObject()) {
            if (!item.contains("from") || !item.contains("to")) {
                continue;
            }
            from = static_cast<float>(item["from"].asDouble().unwrapOr(-1.0));
            to = static_cast<float>(item["to"].asDouble().unwrapOr(-1.0));
        } else {
            continue;
        }
        out.push_back({from, to});
        if (static_cast<int>(out.size()) >= EbStageConfig::ASSIST_OFF_MAX) {
            break;
        }
    }
    normalizeAssistOffRanges(out);
    return true;
}

std::string EbStageConfigImplementation::serializeAssistOffRangesJson(
    std::vector<EbStageConfig::PercentRange> const& ranges) {
    matjson::Value root = matjson::Value::array();
    for (auto const& range : ranges) {
        matjson::Value pair = matjson::Value::array();
        pair.push(static_cast<double>(range.from));
        pair.push(static_cast<double>(range.to));
        root.push(pair);
    }
    return root.dump();
}

void EbStageConfigImplementation::loadAssistOffFromLegacy(
    geode::prelude::Mod* mod, std::vector<EbStageConfig::PercentRange>& out) {
    out.clear();
    if (!mod) {
        return;
    }
    static char const* fromIds[] = {"assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
                                    "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"};
    static char const* toIds[] = {"assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
                                  "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"};
    static_assert(sizeof(fromIds) / sizeof(fromIds[0]) == EbStageConfig::ASSIST_OFF_SLOTS);
    for (int index = 0; index < EbStageConfig::ASSIST_OFF_SLOTS; ++index) {
        float firstValue = readFloat(mod, fromIds[index], -1.f);
        float secondValue = readFloat(mod, toIds[index], -1.f);
        if (firstValue < 0.f || secondValue < 0.f) {
            continue;
        }
        out.push_back({firstValue, secondValue});
    }
    normalizeAssistOffRanges(out);
}

void EbStageConfigImplementation::writeAssistOffRangesJson(
    geode::prelude::Mod* mod, std::vector<EbStageConfig::PercentRange> const& ranges) {
    if (!mod) {
        return;
    }
    std::string const json = serializeAssistOffRangesJson(ranges);
    bool const nested = s_applying;
    if (!nested) {
        s_applying = true;
    }
    try {
        mod->setSettingValue<std::string>("assist-off-ranges", json);
    } catch (...) {
    }
    if (!nested) {
        s_applying = false;
    }
}

void EbStageConfigImplementation::mirrorAssistOffLegacySlots(
    geode::prelude::Mod* mod, std::vector<EbStageConfig::PercentRange> const& ranges) {
    if (!mod) {
        return;
    }
    static char const* fromIds[] = {"assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
                                    "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"};
    static char const* toIds[] = {"assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
                                  "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"};
    static_assert(sizeof(fromIds) / sizeof(fromIds[0]) == EbStageConfig::ASSIST_OFF_SLOTS);

    bool const nested = s_applying;
    if (!nested) {
        s_applying = true;
    }
    for (int index = 0; index < EbStageConfig::ASSIST_OFF_SLOTS; ++index) {
        double fromValue = -1.0;
        double toValue = -1.0;
        if (index < static_cast<int>(ranges.size())) {
            fromValue = static_cast<double>(ranges[static_cast<size_t>(index)].from);
            toValue = static_cast<double>(ranges[static_cast<size_t>(index)].to);
        }
        try {
            mod->setSettingValue<double>(fromIds[index], fromValue);
            mod->setSettingValue<double>(toIds[index], toValue);
        } catch (...) {
        }
    }
    if (!nested) {
        s_applying = false;
    }
}

void EbStageConfigImplementation::loadAssistOffSlots(geode::prelude::Mod* mod,
                                                     std::vector<EbStageConfig::PercentRange>& out) {
    out.clear();
    if (!mod) {
        return;
    }

    std::string raw;
    try {
        raw = mod->getSettingValue<std::string>("assist-off-ranges");
    } catch (...) {
        raw.clear();
    }

    if (parseAssistOffRangesJson(raw, out)) {
        return;
    }

    // Empty / invalid JSON → import legacy float pairs, then persist JSON once.
    loadAssistOffFromLegacy(mod, out);
    writeAssistOffRangesJson(mod, out);
}

void EbStageConfig::refreshCache() {
    return EbStageSettingsCache::refreshCache();
}

void EbStageConfigImplementation::ensureCache() {
    if (!EbStageConfigImplementation::s_cacheInitialized)
        EbStageConfig::refreshCache();
}

void EbStageConfig::toggleDebugLogging() {
    EbStageConfigImplementation::ensureCache();
    EbStageConfigImplementation::s_cachedDebug = !EbStageConfigImplementation::s_cachedDebug;
    geode::prelude::Mod::get()->setSettingValue<bool>("debug-logging",
                                                      EbStageConfigImplementation::s_cachedDebug);
}

void EbStageConfig::toggleAutoTune() {
    EbStageConfigImplementation::ensureCache();
    EbStageConfigImplementation::s_cachedAutoTune = !EbStageConfigImplementation::s_cachedAutoTune;
    geode::prelude::Mod::get()->setSettingValue<bool>("auto-tune",
                                                      EbStageConfigImplementation::s_cachedAutoTune);
}

char const* EbStageConfig::assistModeName(AssistMode mode) {
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

char const* EbStageConfig::assistModeHint(AssistMode mode) {
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

void EbStageConfig::setAssistMode(AssistMode mode) {
    EbStageConfigImplementation::ensureCache();
    EbStageConfigImplementation::s_cachedAssistMode = mode;
    geode::prelude::Mod::get()->setSettingValue<std::string>("assist-mode",
                                                             std::string(assistModeName(mode)));
}

void EbStageConfig::cycleAssistMode() {
    EbStageConfigImplementation::ensureCache();
    int next = (static_cast<int>(EbStageConfigImplementation::s_cachedAssistMode) + 1) % 4;
    setAssistMode(static_cast<AssistMode>(next));
}

void EbStageConfig::toggleShowTrajectory() {
    EbStageConfigImplementation::ensureCache();
    EbStageConfigImplementation::s_cachedShowTrajectory =
        !EbStageConfigImplementation::s_cachedShowTrajectory;
    geode::prelude::Mod::get()->setSettingValue<bool>(
        "show-trajectory", EbStageConfigImplementation::s_cachedShowTrajectory);
}

bool EbStageConfig::isApplying() {
    return EbStageConfigImplementation::s_applying;
}

void EbStageConfig::beginApplying() {
    EbStageConfigImplementation::s_applying = true;
}

void EbStageConfig::endApplying() {
    EbStageConfigImplementation::s_applying = false;
}

EbStageInfo EbStageConfig::fromSettings() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedStage;
}

bool EbStageConfig::isEnabled() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedStage.enabled;
}
bool EbStageConfig::debugLogging() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedDebug;
}
bool EbStageConfig::perfTrace() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedPerfTrace;
}
bool EbStageConfig::lagProbe() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedLagProbe;
}
bool EbStageConfig::safeMode() {
    // Critical for progress gates: always re-read so pause toggles apply mid-run.
    if (auto* mod = geode::prelude::Mod::get()) {
        try {
            EbStageConfigImplementation::s_cachedSafeMode = mod->getSettingValue<bool>("safe-mode");
            EbStageConfigImplementation::s_cacheInitialized = true;
            return EbStageConfigImplementation::s_cachedSafeMode;
        } catch (...) {
        }
    }
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSafeMode;
}

bool EbStageConfig::blocksProgressSaves(bool assistTouchedRun) {
    bool safe = true;
    bool enabled = true;
    if (auto* mod = geode::prelude::Mod::get()) {
        try {
            safe = mod->getSettingValue<bool>("safe-mode");
            enabled = mod->getSettingValue<bool>("enabled");
            EbStageConfigImplementation::s_cachedSafeMode = safe;
            EbStageConfigImplementation::s_cachedStage.enabled = enabled;
            EbStageConfigImplementation::s_cacheInitialized = true;
        } catch (...) {
            EbStageConfigImplementation::ensureCache();
            safe = EbStageConfigImplementation::s_cachedSafeMode;
            enabled = EbStageConfigImplementation::s_cachedStage.enabled;
        }
    } else {
        EbStageConfigImplementation::ensureCache();
        safe = EbStageConfigImplementation::s_cachedSafeMode;
        enabled = EbStageConfigImplementation::s_cachedStage.enabled;
    }

    // Safe Mode OFF → never block percentage / bests / normal complete.
    if (!safe) {
        return false;
    }
    return enabled || assistTouchedRun;
}

bool EbStageConfig::showOverlay() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedShowOverlay;
}
bool EbStageConfig::showTrajectory() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedShowTrajectory;
}
bool EbStageConfig::showBakePath() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedShowBakePath;
}
bool EbStageConfig::showPauseBake() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedShowPauseBake;
}
bool EbStageConfig::showPauseHub() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedShowPauseHub;
}
bool EbStageConfig::assistPress() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistPress;
}
bool EbStageConfig::assistRelease() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistRelease;
}
bool EbStageConfig::assistOrbs() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistOrbs;
}
bool EbStageConfig::autoTune() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAutoTune;
}
bool EbStageConfig::selfBake() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSelfBake;
}
bool EbStageConfig::selfBakeNormal() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSelfBakeNormal;
}
bool EbStageConfig::selfBakeRefine() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSelfBakeRefine;
}
bool EbStageConfig::triggerBake() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedTriggerBake;
}
bool EbStageConfig::triggerBakeSkipDynamic() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedTriggerBakeSkipDynamic;
}
AssistMode EbStageConfig::assistMode() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistMode;
}

bool EbStageConfig::assistPickLegacy() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistPickLegacy;
}

bool EbStageConfig::assistPickV1() {
    return assistPickLegacy();
}

char const* EbStageConfig::assistPickName() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistPickLegacy ? "v1" : "v2";
}

char const* EbStageConfig::canonicalizeAssistPick(std::string const& raw) {
    if (raw == "v1" || raw == "Legacy")
        return "v1";
    if (!(raw == "v2" || raw == "New")) {
        return nullptr;
    }
    return "v2";
}

EbStageConfig::AssistEngineKind EbStageConfig::assistEngine() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistEngine;
}

EbStageConfig::AssistEngineKind EbStageConfig::effectiveAssistEngine() {
    EbStageConfigImplementation::ensureCache();
    // V1 choose is frozen on full ghost runOne — never Trajectory.
    if (!(EbStageConfigImplementation::s_cachedAssistPickLegacy)) {
        return EbStageConfigImplementation::s_cachedAssistEngine;
    }
    return AssistEngineKind::ShadowPlayer;
}

char const* EbStageConfig::assistEngineName(AssistEngineKind engine) {
    switch (engine) {
    case AssistEngineKind::Trajectory:
        return "Trajectory";
    case AssistEngineKind::ShadowPlayer:
    default:
        return "ShadowPlayer";
    }
}

char const* EbStageConfig::assistEngineName() {
    return assistEngineName(effectiveAssistEngine());
}
int EbStageConfig::simBudgetUs() {
    EbStageConfigImplementation::ensureCache();
    return EbPlatformLimits::get().clampSimBudgetUs(EbStageConfigImplementation::s_cachedSimBudgetUs);
}
int EbStageConfig::bakeRadius() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedBakeRadius;
}
int EbStageConfig::cpsLimit() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedCpsLimit;
}
int EbStageConfig::minClickIntervalMs() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedMinClickIntervalMs;
}
int EbStageConfig::smartDepth() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSmartDepth;
}
int EbStageConfig::smartCombos() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSmartCombos;
}
int EbStageConfig::smartHorizon() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSmartHorizon;
}
int EbStageConfig::robustK() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedRobustK;
}
bool EbStageConfig::robustJitter() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedRobustJitter;
}
bool EbStageConfig::adaptivePlan() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAdaptivePlan;
}
bool EbStageConfig::assistProbePastHorizon() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistProbePastHorizon;
}
bool EbStageConfig::assistMicroWindow() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistMicroWindow;
}
int EbStageConfig::adaptiveScreenTicks() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAdaptiveScreenTicks;
}
int EbStageConfig::adaptiveVerifyQuantum() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAdaptiveVerifyQuantum;
}
int EbStageConfig::adaptiveReservePct() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAdaptiveReservePct;
}
bool EbStageConfig::adaptivePlanFor(GameModeKind kind) {
    return paramsFor(kind).adaptivePlan;
}
int EbStageConfig::adaptiveScreenTicksFor(GameModeKind kind) {
    return paramsFor(kind).adaptiveScreenTicks;
}
int EbStageConfig::adaptiveVerifyQuantumFor(GameModeKind kind) {
    return paramsFor(kind).adaptiveVerifyQuantum;
}
int EbStageConfig::adaptiveReservePctFor(GameModeKind kind) {
    return paramsFor(kind).adaptiveReservePct;
}
bool EbStageConfig::adaptivePlanFor(PlayerObject* player) {
    return adaptivePlanFor(kindOf(player));
}
int EbStageConfig::adaptiveScreenTicksFor(PlayerObject* player) {
    return adaptiveScreenTicksFor(kindOf(player));
}
int EbStageConfig::adaptiveVerifyQuantumFor(PlayerObject* player) {
    return adaptiveVerifyQuantumFor(kindOf(player));
}
int EbStageConfig::adaptiveReservePctFor(PlayerObject* player) {
    return adaptiveReservePctFor(kindOf(player));
}

bool EbStageConfig::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    // Pure resolve: overwrite off → global; overwrite on → mode value.
    auto resolveBool = [](bool overwrite, bool modeValue, bool globalValue) {
        return overwrite ? modeValue : globalValue;
    };
    auto resolveInt = [](bool overwrite, int modeValue, int globalValue) {
        return overwrite ? modeValue : globalValue;
    };
    expect(resolveBool(false, false, true) == true, "resolve-off-uses-global");
    expect(resolveBool(true, false, true) == false, "resolve-on-uses-mode");
    expect(resolveInt(false, 8, 24) == 24, "resolve-int-off");
    expect(resolveInt(true, 8, 24) == 8, "resolve-int-on");

    EbStageConfigImplementation::ensureCache();
    bool const globalPlan = adaptivePlan();
    int const globalScreen = adaptiveScreenTicks();
    int const globalVerify = adaptiveVerifyQuantum();
    int const globalReserve = adaptiveReservePct();
    for (int index = 0; index < MODE_COUNT; ++index) {
        auto const kind = static_cast<GameModeKind>(index);
        EbModeParams const params = paramsFor(kind);
        expect(adaptivePlanFor(kind) == params.adaptivePlan, "for-matches-params-plan");
        expect(adaptiveScreenTicksFor(kind) == params.adaptiveScreenTicks,
               "for-matches-params-screen");
        expect(adaptiveVerifyQuantumFor(kind) == params.adaptiveVerifyQuantum,
               "for-matches-params-verify");
        expect(adaptiveReservePctFor(kind) == params.adaptiveReservePct,
               "for-matches-params-reserve");
        if (!params.adaptiveOverwrite) {
            expect(params.adaptivePlan == globalPlan, "fallback-plan");
            expect(params.adaptiveScreenTicks == globalScreen, "fallback-screen");
            expect(params.adaptiveVerifyQuantum == globalVerify, "fallback-verify");
            expect(params.adaptiveReservePct == globalReserve, "fallback-reserve");
        } else {
            expect(params.adaptiveScreenTicks >= 8 && params.adaptiveScreenTicks <= 64,
                   "mode-screen-range");
            expect(params.adaptiveVerifyQuantum >= 8 && params.adaptiveVerifyQuantum <= 64,
                   "mode-verify-range");
            expect(params.adaptiveReservePct >= 0 && params.adaptiveReservePct <= 40,
                   "mode-reserve-range");
        }
    }

    if (fails == 0) {
        report = "StageConfig adaptive resolve selfCheck OK";
        return true;
    }
    report = std::string("StageConfig adaptive resolve selfCheck fails: ") + report;
    return false;
}
char const* EbStageConfig::assistVerifyMode() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistVerify.c_str();
}
bool EbStageConfig::assistVerifyAb() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistVerify == "ab";
}
int EbStageConfig::tickBudgetPerPress() {
    EbStageConfigImplementation::ensureCache();
    return EbPlatformLimits::get().clampTickBudget(EbStageConfigImplementation::s_cachedTickBudget);
}
int EbStageConfig::husReplanBudgetUs() {
    int const full = simBudgetUs();
    if (!(full <= 0)) {
        return std::max(200, full / 2);
    }
    return 200;
}
int EbStageConfig::husReplanTickBudget() {
    return std::max(200, tickBudgetPerPress() / 4);
}
bool EbStageConfig::assistOnlyPractice() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistOnlyPractice;
}
bool EbStageConfig::assistStartposOnly() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistStartposOnly;
}
int EbStageConfig::assistPercentMin() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistPercentMin;
}
int EbStageConfig::assistPercentMax() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistPercentMax;
}
std::vector<EbStageConfig::PercentRange> const& EbStageConfig::assistDisableRanges() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedAssistDisableRanges;
}

bool EbStageConfig::setAssistDisableRanges(std::vector<PercentRange> const& ranges) {
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        return false;
    }
    std::vector<PercentRange> next = ranges;
    EbStageConfigImplementation::normalizeAssistOffRanges(next);

    EbStageConfigImplementation::ensureCache();
    if (next.size() == EbStageConfigImplementation::s_cachedAssistDisableRanges.size()) {
        bool same = true;
        for (size_t index = 0; index < next.size(); ++index) {
            if (next[index].from != EbStageConfigImplementation::s_cachedAssistDisableRanges[index].from ||
                next[index].to != EbStageConfigImplementation::s_cachedAssistDisableRanges[index].to) {
                same = false;
                break;
            }
        }
        if (same) {
            return false;
        }
    }

    bool const nested = EbStageConfigImplementation::s_applying;
    if (!nested) {
        EbStageConfigImplementation::s_applying = true;
    }
    EbStageConfigImplementation::writeAssistOffRangesJson(mod, next);
    EbStageConfigImplementation::mirrorAssistOffLegacySlots(mod, next);
    if (!nested) {
        EbStageConfigImplementation::s_applying = false;
    }
    EbStageConfigImplementation::s_cachedAssistDisableRanges = next;
    return true;
}

void EbStageConfig::rebuildAssistOffRangesFromLegacySlots() {
    if (EbStageConfigImplementation::s_applying) {
        return;
    }
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        return;
    }
    std::vector<PercentRange> next;
    EbStageConfigImplementation::loadAssistOffFromLegacy(mod, next);
    EbStageConfigImplementation::s_applying = true;
    EbStageConfigImplementation::writeAssistOffRangesJson(mod, next);
    EbStageConfigImplementation::s_applying = false;
    EbStageConfigImplementation::s_cachedAssistDisableRanges = std::move(next);
}

bool EbStageConfig::isInAssistDisableRange(float percent) {
    EbStageConfigImplementation::ensureCache();
    float const valuePointer = EbStageConfigImplementation::clampPct(percent);
    for (auto const& r : EbStageConfigImplementation::s_cachedAssistDisableRanges) {
        if (valuePointer + 0.0001f >= r.from && valuePointer - 0.0001f <= r.to)
            return true;
    }
    return false;
}
bool EbStageConfig::saveFeedbackSound() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSaveFeedbackSound;
}
bool EbStageConfig::saveFeedbackIcon() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSaveFeedbackColor;
}
bool EbStageConfig::saveFeedbackCounter() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSaveFeedbackCounter;
}
EbStageConfig::ColorRgb EbStageConfig::saveFeedbackColor() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSaveIconColor;
}
bool EbStageConfig::windowUnitFrames() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedWindowUnitFrames;
}

int EbStageConfig::windowValueToTicks(int value) {
    EbStageConfigImplementation::ensureCache();
    if (value <= 0)
        return 0;
    if (!(!EbStageConfigImplementation::s_cachedWindowUnitFrames)) {
        return EbAssistClock::framesToTicks(value);
    }
    return value;
}

int EbStageConfig::ticksToWindowValue(int ticks) {
    EbStageConfigImplementation::ensureCache();
    if (ticks <= 0)
        return 0;
    if (!(!EbStageConfigImplementation::s_cachedWindowUnitFrames)) {
        return EbAssistClock::ticksToFrames(ticks);
    }
    return ticks;
}

int EbStageConfig::ticksAll() {
    return windowValueToTicks(EbStageConfigImplementation::s_cachedStage.ticksAll);
}
int EbStageConfig::ticksWave() {
    return windowValueToTicks(EbStageConfigImplementation::s_cachedStage.ticksWave);
}
int EbStageConfig::deathSlack() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedSlack;
}
int EbStageConfig::lookAheadCap() {
    EbStageConfigImplementation::ensureCache();
    return EbStageConfigImplementation::s_cachedLookAhead;
}
GameModeKind EbStageConfig::kindOf(PlayerObject* player) {
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

char const* EbStageConfig::kindName(GameModeKind kind) {
    int const index = static_cast<int>(kind);
    if (!(index < 0 || index >= MODE_COUNT)) {
        return EbStageConfigImplementation::MODE_IDS[index].name;
    }
    return "cube";
}

EbModeParams EbStageConfig::paramsFor(GameModeKind kind) {
    EbStageConfigImplementation::ensureCache();
    int const index = static_cast<int>(kind);
    if (!(index < 0 || index >= MODE_COUNT)) {
        return EbStageConfigImplementation::s_cachedModes[static_cast<size_t>(index)];
    }
    return EbStageConfigImplementation::s_cachedModes[0];
}

EbModeParams EbStageConfig::paramsFor(PlayerObject* player) {
    return paramsFor(kindOf(player));
}

std::string EbStageConfig::stageLabel(EbStageInfo const& info) {
    if (!(!info.enabled)) {
        return fmt::format("{}A/{}W", info.ticksAll, info.ticksWave);
    }
    return "OFF";
}
