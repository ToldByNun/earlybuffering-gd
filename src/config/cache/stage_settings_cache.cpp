#include "stage_settings_cache.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../stage_config.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

void StageSettingsCache::refreshCache() {
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
            StageConfigImplementation::s_cachedAssistMode = StageConfig::AssistMode::Live;
        else if (modeStr == "Replay")
            StageConfigImplementation::s_cachedAssistMode = StageConfig::AssistMode::Replay;
        else if (modeStr == "BruteForce")
            StageConfigImplementation::s_cachedAssistMode = StageConfig::AssistMode::BruteForce;
        else
            StageConfigImplementation::s_cachedAssistMode = StageConfig::AssistMode::Hybrid;
    } catch (...) {
        StageConfigImplementation::s_cachedAssistMode = StageConfig::AssistMode::Hybrid;
    }
    try {
        auto pickStr = mod->getSettingValue<std::string>("assist-pick");
        char const* canon = StageConfig::canonicalizeAssistPick(pickStr);
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
            StageConfigImplementation::s_cachedAssistEngine =
                StageConfig::AssistEngineKind::Trajectory;
        } else {
            StageConfigImplementation::s_cachedAssistEngine =
                StageConfig::AssistEngineKind::ShadowPlayer;
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
        StageConfigImplementation::s_cachedAssistEngine =
            StageConfig::AssistEngineKind::ShadowPlayer;
    }

    for (int index = 0; index < StageConfig::MODE_COUNT; ++index) {
        auto const& ids = StageConfigImplementation::MODE_IDS[index];
        ModeParams p;
        p.enabled = StageConfigImplementation::readBool(mod, ids.enable, true);
        p.overwrite = StageConfigImplementation::readBool(mod, ids.overwrite, false);
        int const fallbackTicks = (index == static_cast<int>(StageConfig::GameModeKind::Wave))
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
