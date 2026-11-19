#include "stage_settings_cache.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../assist_mode_migration.hpp"
#include "../assist_policy_settings.hpp"
#include "../stage_config.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

void EbStageSettingsCache::refreshCache() {
    EbAssistPolicySettings::refresh();
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
    EbStageConfigImplementation::s_cachedLagWarningToast =
        EbStageConfigImplementation::readBool(mod, "lag-warning-toast", true);
    EbStageConfigImplementation::s_cachedShowTrajectory =
        EbStageConfigImplementation::readBool(mod, "show-trajectory", false);
    EbStageConfigImplementation::s_cachedShowStraightfly =
        EbStageConfigImplementation::readBool(mod, "show-straightfly", false);
    EbStageConfigImplementation::s_cachedShowWavePattern =
        EbStageConfigImplementation::readBool(mod, "show-wave-pattern", false);
    EbStageConfigImplementation::s_cachedShowBakePath = false;
    EbStageConfigImplementation::s_cachedShowPauseBake = false;
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
    EbStageConfigImplementation::s_cachedSelfBake = false;
    EbStageConfigImplementation::s_cachedSelfBakeNormal = false;
    EbStageConfigImplementation::s_cachedSelfBakeRefine = false;
    EbStageConfigImplementation::s_cachedTriggerBake =
        EbStageConfigImplementation::readBool(mod, "trigger-bake", true);
    EbStageConfigImplementation::s_cachedTriggerBakeSkipDynamic =
        EbStageConfigImplementation::readBool(mod, "trigger-bake-skip-dynamic", true);
    EbStageConfigImplementation::s_cachedSimBudgetUs = EbStageConfigImplementation::clampInt(
        EbStageConfigImplementation::readInt(mod, "sim-budget-us", 4000), 0, 10000);
    EbStageConfigImplementation::s_cachedBakeRadius = 0;
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
    EbStageConfigImplementation::s_cachedAssistProbePastHorizon =
        EbStageConfigImplementation::readBool(mod, "assist-probe-past-horizon", true);
    EbStageConfigImplementation::s_cachedAssistMicroWindow =
        EbStageConfigImplementation::readBool(mod, "assist-micro-window", true);
    EbStageConfigImplementation::s_cachedAdaptiveScreenTicks =
        EbStageConfigImplementation::clampInt(
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
        EbStageConfigImplementation::s_cachedSaveIconColor = {candidate.r, candidate.g,
                                                              candidate.b};
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
        auto const storedMode = mod->getSettingValue<std::string>("assist-mode");
        std::string const modeStr = EbAssistModeMigration::canonical(storedMode);
        EbStageConfigImplementation::s_cachedAssistMode = modeStr == "Hybrid"
                                                              ? EbStageConfig::AssistMode::Hybrid
                                                              : EbStageConfig::AssistMode::Live;
        if (storedMode != modeStr)
            mod->setSettingValue<std::string>("assist-mode", "Live");
    } catch (...) {
        EbStageConfigImplementation::s_cachedAssistMode = EbStageConfig::AssistMode::Live;
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
        // Adaptive: mode overwrite → mode knobs; else Main Adaptive defaults.
        p.adaptiveOverwrite =
            EbStageConfigImplementation::readBool(mod, ids.adaptiveOverwrite, false);
        if (p.adaptiveOverwrite) {
            p.adaptivePlan = EbStageConfigImplementation::readBool(mod, ids.adaptivePlan, true);
            p.adaptiveScreenTicks = EbStageConfigImplementation::clampInt(
                EbStageConfigImplementation::readInt(mod, ids.adaptiveScreenTicks, 24), 8, 64);
            p.adaptiveVerifyQuantum = EbStageConfigImplementation::clampInt(
                EbStageConfigImplementation::readInt(mod, ids.adaptiveVerifyQuantum, 16), 8, 64);
            p.adaptiveReservePct = EbStageConfigImplementation::clampInt(
                EbStageConfigImplementation::readInt(mod, ids.adaptiveReservePct, 15), 0, 40);
        } else {
            p.adaptivePlan = EbStageConfigImplementation::s_cachedAdaptivePlan;
            p.adaptiveScreenTicks = EbStageConfigImplementation::s_cachedAdaptiveScreenTicks;
            p.adaptiveVerifyQuantum = EbStageConfigImplementation::s_cachedAdaptiveVerifyQuantum;
            p.adaptiveReservePct = EbStageConfigImplementation::s_cachedAdaptiveReservePct;
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
