#include "stage_settings_cache.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../stage_config.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

void EbStageSettingsCache::refreshCache() {
    auto* mod = geode::prelude::Mod::get();
    EbStageConfigImplementation::s_cachedStage.enabled = mod->getSettingValue<bool>("enabled");
    // Wider max when storing display frames (8 frames ≈ 32 ticks @240 TPS).
    EbStageConfigImplementation::s_cachedStage.ticksAll = EbStageConfigImplementation::clampInt(
        static_cast<int>(mod->getSettingValue<int64_t>("ticks-all")), 0, 48);
    EbStageConfigImplementation::s_cachedStage.ticksWave = EbStageConfigImplementation::clampInt(
        static_cast<int>(mod->getSettingValue<int64_t>("ticks-wave")), 0, 48);
    try {
        auto unit = mod->getSettingValue<std::string>("window-unit");
        EbStageConfigImplementation::s_cachedWindowUnitFrames = (unit == "Frames");
    } catch (...) {
        EbStageConfigImplementation::s_cachedWindowUnitFrames = false;
    }

    EbStageConfigImplementation::s_cachedDebug = mod->getSettingValue<bool>("debug-logging");
    EbStageConfigImplementation::s_cachedPerfTrace =
        EbStageConfigImplementation::readBool(mod, "perf-trace", false);
    EbStageConfigImplementation::s_cachedLagProbe =
        EbStageConfigImplementation::readBool(mod, "lag-probe", false);
    EbStageConfigImplementation::s_cachedSafeMode = mod->getSettingValue<bool>("safe-mode");
    EbStageConfigImplementation::s_cachedShowOverlay = mod->getSettingValue<bool>("show-overlay");
    EbStageConfigImplementation::s_cachedShowTrajectory =
        EbStageConfigImplementation::readBool(mod, "show-trajectory", false);
    EbStageConfigImplementation::s_cachedShowBakePath =
        EbStageConfigImplementation::readBool(mod, "show-bake-path", false);
    EbStageConfigImplementation::s_cachedShowPauseBake =
        EbStageConfigImplementation::readBool(mod, "show-pause-bake", false);
    EbStageConfigImplementation::s_cachedShowPauseHub =
        EbStageConfigImplementation::readBool(mod, "show-pause-hub", true);
    EbStageConfigImplementation::s_cachedAssistPress =
        EbStageConfigImplementation::readBool(mod, "assist-press", true);
    EbStageConfigImplementation::s_cachedAssistRelease =
        EbStageConfigImplementation::readBool(mod, "assist-release", true);
    EbStageConfigImplementation::s_cachedAssistOrbs =
        EbStageConfigImplementation::readBool(mod, "assist-orbs", true);
    EbStageConfigImplementation::s_cachedAutoTune =
        EbStageConfigImplementation::readBool(mod, "auto-tune", false);
    EbStageConfigImplementation::s_cachedSelfBake =
        EbStageConfigImplementation::readBool(mod, "self-bake", false);
    EbStageConfigImplementation::s_cachedSelfBakeNormal =
        EbStageConfigImplementation::readBool(mod, "self-bake-normal", false);
    EbStageConfigImplementation::s_cachedSelfBakeRefine =
        EbStageConfigImplementation::readBool(mod, "self-bake-refine", true);
    EbStageConfigImplementation::s_cachedTriggerBake =
        EbStageConfigImplementation::readBool(mod, "trigger-bake", true);
    EbStageConfigImplementation::s_cachedTriggerBakeSkipDynamic =
        EbStageConfigImplementation::readBool(mod, "trigger-bake-skip-dynamic", true);
    EbStageConfigImplementation::s_cachedSimBudgetUs = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "sim-budget-us", 4000), 0, 10000);
    EbStageConfigImplementation::s_cachedBakeRadius = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "bake-radius", 0), 0, 128);
    EbStageConfigImplementation::s_cachedCpsLimit = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "cps-limit", 0), 0, 40);
    EbStageConfigImplementation::s_cachedMinClickIntervalMs = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "min-click-interval-ms", 0), 0, 500);
    EbStageConfigImplementation::s_cachedSmartDepth = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "smart-depth", 1), 1, 3);
    EbStageConfigImplementation::s_cachedSmartCombos = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "smart-combos", 48), 8, 256);
    EbStageConfigImplementation::s_cachedSmartHorizon = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "smart-horizon", 0), 0, 320);
    EbStageConfigImplementation::s_cachedRobustK = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "robust-k", 0), 0, 3);
    EbStageConfigImplementation::s_cachedRobustJitter =
        EbStageConfigImplementation::readBool(mod, "robust-jitter", false);
    EbStageConfigImplementation::s_cachedAdaptivePlan =
        EbStageConfigImplementation::readBool(mod, "adaptive-plan", true);
    EbStageConfigImplementation::s_cachedAdaptiveScreenTicks = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "adaptive-screen-ticks", 24), 8, 64);
    EbStageConfigImplementation::s_cachedAdaptiveVerifyQuantum =
        EbStageConfigImplementation::clampInt(
            EbStageConfigImplementation::readInt(mod, "adaptive-verify-quantum", 16), 8, 64);
    EbStageConfigImplementation::s_cachedAdaptiveReservePct = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "adaptive-reserve-pct", 15), 0, 40);
    EbStageConfigImplementation::s_cachedTickBudget = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "tick-budget", 4000), 500, 20000);
    EbStageConfigImplementation::s_cachedAssistOnlyPractice =
        EbStageConfigImplementation::readBool(mod, "assist-only-practice", false);
    EbStageConfigImplementation::s_cachedAssistStartposOnly =
        EbStageConfigImplementation::readBool(mod, "assist-startpos-only", false);
    EbStageConfigImplementation::s_cachedAssistPercentMin = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "assist-percent-min", 0), 0, 100);
    EbStageConfigImplementation::s_cachedAssistPercentMax = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "assist-percent-max", 100), 0, 100);
    if (EbStageConfigImplementation::s_cachedAssistPercentMin >
        EbStageConfigImplementation::s_cachedAssistPercentMax) {
        std::swap(EbStageConfigImplementation::s_cachedAssistPercentMin,
                  EbStageConfigImplementation::s_cachedAssistPercentMax);
    }
    EbStageConfigImplementation::loadAssistOffSlots(
        mod, EbStageConfigImplementation::s_cachedAssistDisableRanges);
    EbStageConfigImplementation::s_cachedSaveFeedbackSound =
        EbStageConfigImplementation::readBool(mod, "save-feedback-sound", false);
    EbStageConfigImplementation::s_cachedSaveFeedbackColor =
        EbStageConfigImplementation::readBool(mod, "save-feedback-color", false);
    EbStageConfigImplementation::s_cachedSaveFeedbackCounter =
        EbStageConfigImplementation::readBool(mod, "save-feedback-counter", false);
    try {
        auto const candidate = mod->getSettingValue<cocos2d::ccColor3B>("save-icon-color");
        EbStageConfigImplementation::s_cachedSaveIconColor = {candidate.r, candidate.g, candidate.b};
    } catch (...) {
        EbStageConfigImplementation::s_cachedSaveIconColor = {};
    }
    try {
        EbStageConfigImplementation::s_cachedAssistVerify =
            mod->getSettingValue<std::string>("assist-verify");
        if (EbStageConfigImplementation::s_cachedAssistVerify != "ab")
            EbStageConfigImplementation::s_cachedAssistVerify = "off";
    } catch (...) {
        EbStageConfigImplementation::s_cachedAssistVerify = "off";
    }
    EbStageConfigImplementation::s_cachedSlack = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "death-slack", 2), 0, 16);
    EbStageConfigImplementation::s_cachedLookAhead = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "look-ahead-cap", 0), 0, 240);
    try {
        auto modeStr = mod->getSettingValue<std::string>("assist-mode");
        if (modeStr == "Live")
            EbStageConfigImplementation::s_cachedAssistMode = EbStageConfig::AssistMode::Live;
        else if (modeStr == "Replay")
            EbStageConfigImplementation::s_cachedAssistMode = EbStageConfig::AssistMode::Replay;
        else if (modeStr == "BruteForce")
            EbStageConfigImplementation::s_cachedAssistMode = EbStageConfig::AssistMode::BruteForce;
        else
            EbStageConfigImplementation::s_cachedAssistMode = EbStageConfig::AssistMode::Hybrid;
    } catch (...) {
        EbStageConfigImplementation::s_cachedAssistMode = EbStageConfig::AssistMode::Hybrid;
    }
    try {
        auto pickStr = mod->getSettingValue<std::string>("assist-pick");
        char const* canon = EbStageConfig::canonicalizeAssistPick(pickStr);
        if (!canon) {
            // Unknown → v1 default.
            EbStageConfigImplementation::s_cachedAssistPickLegacy = true;
            canon = "v1";
        } else {
            EbStageConfigImplementation::s_cachedAssistPickLegacy = (std::strcmp(canon, "v1") == 0);
        }
        // Migrate store Legacy/New → v1/v2 so Geode one-of stays valid.
        if (pickStr != canon) {
            bool const nested = EbStageConfigImplementation::s_applying;
            if (!nested)
                EbStageConfigImplementation::s_applying = true;
            try {
                mod->setSettingValue<std::string>("assist-pick", std::string(canon));
            } catch (...) {
            }
            if (!nested)
                EbStageConfigImplementation::s_applying = false;
        }
    } catch (...) {
        // mod.json default is v1.
        EbStageConfigImplementation::s_cachedAssistPickLegacy = true;
    }
    try {
        auto engStr = mod->getSettingValue<std::string>("assist-engine");
        if (engStr == "Trajectory") {
            EbStageConfigImplementation::s_cachedAssistEngine =
                EbStageConfig::AssistEngineKind::Trajectory;
        } else {
            EbStageConfigImplementation::s_cachedAssistEngine =
                EbStageConfig::AssistEngineKind::ShadowPlayer;
            if (engStr != "ShadowPlayer") {
                bool const nested = EbStageConfigImplementation::s_applying;
                if (!nested)
                    EbStageConfigImplementation::s_applying = true;
                try {
                    mod->setSettingValue<std::string>("assist-engine", std::string("ShadowPlayer"));
                } catch (...) {
                }
                if (!nested)
                    EbStageConfigImplementation::s_applying = false;
            }
        }
    } catch (...) {
        EbStageConfigImplementation::s_cachedAssistEngine =
            EbStageConfig::AssistEngineKind::ShadowPlayer;
    }

    for (int index = 0; index < EbStageConfig::MODE_COUNT; ++index) {
        auto const& ids = EbStageConfigImplementation::MODE_IDS[index];
        EbModeParams p;
        p.enabled = EbStageConfigImplementation::readBool(mod, ids.enable, true);
        p.overwrite = EbStageConfigImplementation::readBool(mod, ids.overwrite, false);
        int const fallbackTicks = (index == static_cast<int>(EbStageConfig::GameModeKind::Wave))
                                      ? EbStageConfigImplementation::s_cachedStage.ticksWave
                                      : EbStageConfigImplementation::s_cachedStage.ticksAll;
        float const latDefault = EbStageConfigImplementation::defaultLatencyWeight(index);
        if (p.overwrite) {
            p.ticks = EbStageConfigImplementation::clampInt(
                EbStageConfigImplementation::readInt(mod, ids.ticks, fallbackTicks), 0, 48);
            p.cap = EbStageConfigImplementation::clampInt(
                EbStageConfigImplementation::readInt(mod, ids.cap, 0), 0, 240);
            int slack = EbStageConfigImplementation::readInt(mod, ids.slack, -1);
            p.slack = slack < 0 ? EbStageConfigImplementation::s_cachedSlack
                                : EbStageConfigImplementation::clampInt(slack, 0, 16);
            float lat = EbStageConfigImplementation::readFloat(mod, ids.latency, latDefault);
            p.latencyWeight = std::max(0.f, std::min(2.f, lat));
        } else {
            p.ticks = fallbackTicks;
            p.cap = EbStageConfigImplementation::s_cachedLookAhead;
            p.slack = EbStageConfigImplementation::s_cachedSlack;
            p.latencyWeight = latDefault;
        }
        // Settings store window-unit values; planner always wants physics ticks.
        // Use StageConfigImplementation::s_cachedWindowUnitFrames directly (avoid ensureCache
        // recursion).
        if (EbStageConfigImplementation::s_cachedWindowUnitFrames && p.ticks > 0) {
            p.ticks = EbAssistClock::framesToTicks(p.ticks);
        }
        EbStageConfigImplementation::s_cachedModes[static_cast<size_t>(index)] = p;
    }

    EbStageConfigImplementation::s_cacheInitialized = true;
}
