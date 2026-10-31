#include "config/StageConfig.hpp"
#include "core/AssistController.hpp"
#include "core/BakeMap.hpp"
#include "core/SaveDropPaths.hpp"
#include "core/ShadowSimulator.hpp"
#include "core/TriggerClassify.hpp"
#include "core/TriggerWorldStore.hpp"
#include "debug/AssistLog.hpp"
#include "debug/LagProbe.hpp"
#include "debug/PerfTrace.hpp"
#include "debug/PlanGoldCorpus.hpp"
#include "dev/DevConfig.hpp"
#include "ui/ActionButtonSetting.hpp"
#include "ui/InLevelSettingsFilter.hpp"
#include "ui/StatusOverlay.hpp"
#include "ui/TrajectoryVisualizer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

static void syncPerfTrace() {
    PerfTrace::get().setEnabled(StageConfig::perfTrace());
}

static void syncLagProbe() {
    LagProbe::get().setEnabled(StageConfig::lagProbe());
}

static void onAnySetting() {
    if (StageConfig::isApplying()) return;
    StageConfig::refreshCache();
    DevConfig::refreshCache();
    syncPerfTrace();
    syncLagProbe();
    AssistController::get().updateOverlay();
}



$on_mod(Loaded) {
    // Custom eb-action rows (Bake/Assist buttons) — must resolve before settings UI opens.
    registerActionButtonSettingType();
    // Tabs for every EarlyBuffer settings open (Geode menu + pause EB).
    InLevelSettingsFilterKeeper::startWatchdog();

    // Read saved values only — never re-apply a preset on load (that wiped custom ticks).
    StageConfig::refreshCache();
    DevConfig::refreshCache();
    syncPerfTrace();
    syncLagProbe();
    // Mobile drop folders (replay-drop / config) — soft-fail if save dir is read-only.
    {
        std::string dropErr;
        if (!SaveDropPaths::get().ensureDirs(dropErr) && !dropErr.empty()) {
            AssistLog::info(fmt::format("WARN SaveDropPaths: {}", dropErr));
        }
    }
    AssistLog::printBanner();
    if (StageConfig::perfTrace()) {
        AssistLog::info(fmt::format("PerfTrace logging to {}", PerfTrace::logPath()));
    }
    if (StageConfig::lagProbe()) {
        AssistLog::info(fmt::format("LagProbe logging to {}", LagProbe::logPath()));
    }

    {
        std::string report;
        bool const ok = ShadowSimulator::regressionSelfCheck(report);
        if (ok) AssistLog::info(report);
        else AssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = TriggerClassify::selfCheck(report);
        if (ok) AssistLog::info(report);
        else AssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = TriggerWorldStore::selfCheck(report);
        if (ok) AssistLog::info(report);
        else AssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = PlanGoldCorpus::run(report);
        if (ok) AssistLog::info(report);
        else AssistLog::info(fmt::format("WARN {}", report));
    }

    listenForKeybindSettingPresses("hotkey-cycle", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        auto const stage = StageConfig::cycleNext();
        AssistLog::printCycle(stage, AssistController::get().estimatedTps());
        AssistController::get().updateOverlay();
    });

    listenForKeybindSettingPresses("hotkey-debug", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        StageConfig::toggleDebugLogging();
        bool const now = StageConfig::debugLogging();
        AssistLog::info(fmt::format("debug-logging: {}", now ? "ON" : "OFF"));
    });

    listenForKeybindSettingPresses("hotkey-reset-level", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        AssistController::get().resetLevelProfile();
        AssistLog::info("LEARN: level profile reset (Ctrl+Shift+R)");
    });

    listenForKeybindSettingPresses("hotkey-auto-tune", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        StageConfig::toggleAutoTune();
        bool const now = StageConfig::autoTune();
        AssistLog::info(fmt::format("LEARN: smart learning {}", now ? "ON" : "OFF"));
        AssistController::get().updateOverlay();
    });

    listenForKeybindSettingPresses("hotkey-assist-mode", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        StageConfig::cycleAssistMode();
        auto mode = StageConfig::assistMode();
        AssistLog::info(fmt::format(
            "ASSIST MODE: {} — {}",
            StageConfig::assistModeName(mode),
            StageConfig::assistModeHint(mode)
        ));
        AssistController::get().updateOverlay();
    });

    listenForKeybindSettingPresses("hotkey-trajectory", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        StageConfig::toggleShowTrajectory();
        bool const on = StageConfig::showTrajectory();
        TrajectoryVisualizer::get().setEnabled(on);
        if (on) {
            if (auto* pl = PlayLayer::get()) {
                TrajectoryVisualizer::get().attach(pl);
            }
        }
        AssistLog::info(fmt::format("TRAJ: visualizer {}", on ? "ON" : "OFF"));
        AssistController::get().updateOverlay();
    });

    // Preset dropdown: only fill defaults when the user actually picks a *different* preset.
    listenForSettingChanges<std::string>("preset", [](std::string value) {
        if (StageConfig::isApplying()) return;
        if (value == StageConfig::lastSyncedPreset()) {
            onAnySetting();
            return;
        }
        StageConfig::applyPresetByName(value);
        AssistController::get().updateOverlay();
    });

    // stage-index is written by applyPreset / F6 — never drive applyPreset from it
    // (that was overwriting saved ticks when Geode re-notified the index).
    listenForSettingChanges<int64_t>("stage-index", [](int64_t) {
        onAnySetting();
    });

    listenForSettingChanges<bool>("show-overlay", [](bool value) {
        if (StageConfig::isApplying()) return;
        StageConfig::refreshCache();
        StatusOverlay::setVisible(value);
        AssistController::get().updateOverlay();
    });

    auto watchBool = [](char const* id) {
        listenForSettingChanges<bool>(id, [](bool) { onAnySetting(); });
    };
    auto watchInt = [](char const* id) {
        listenForSettingChanges<int64_t>(id, [](int64_t) { onAnySetting(); });
    };

    watchBool("enabled");
    watchInt("ticks-all");
    listenForSettingChanges<std::string>("window-unit", [](std::string) {
        onAnySetting();
    });
    watchInt("ticks-wave");
    watchInt("death-slack");
    watchInt("look-ahead-cap");
    watchBool("debug-logging");
    watchBool("perf-trace");
    watchBool("lag-probe");
    watchBool("safe-mode");

    listenForSettingChanges<bool>("show-trajectory", [](bool value) {
        if (StageConfig::isApplying()) return;
        StageConfig::refreshCache();
        TrajectoryVisualizer::get().setEnabled(value);
        if (value) {
            if (auto* pl = PlayLayer::get()) TrajectoryVisualizer::get().attach(pl);
        }
        else {
            TrajectoryVisualizer::get().clear();
        }
        AssistController::get().updateOverlay();
    });
    watchBool("show-pause-bake");
    watchBool("show-pause-hub");
    watchBool("auto-tune");
    watchBool("self-bake");
    watchBool("self-bake-normal");
    watchBool("self-bake-refine");
    watchBool("trigger-bake");
    watchBool("trigger-bake-skip-dynamic");
    watchBool("assist-only-practice");
    watchBool("assist-startpos-only");
    watchInt("assist-percent-min");
    watchInt("assist-percent-max");
    watchInt("sim-budget-us");
    watchInt("bake-radius");
    watchInt("cps-limit");
    watchInt("min-click-interval-ms");
    watchBool("assist-orbs");
    watchBool("assist-press");
    watchBool("assist-release");
    watchInt("tick-budget");
    watchInt("smart-depth");
    watchInt("smart-combos");
    watchInt("smart-horizon");
    watchInt("robust-k");
    watchBool("robust-jitter");

    listenForSettingChanges<std::string>("assist-mode", [](std::string) {
        onAnySetting();
    });
    listenForSettingChanges<std::string>("assist-pick", [](std::string) {
        onAnySetting();
    });
    listenForSettingChanges<std::string>("assist-verify", [](std::string) {
        onAnySetting();
    });

    // Dev Lab (Commit 2 cache; session/eval still stubbed).
    watchBool("dev-record-enable");
    watchInt("dev-record-count");
    watchBool("dev-count-empty-attempts");
    watchBool("dev-freeze-world");
    watchBool("dev-disable-random");
    watchBool("dev-eval-on-death");
    watchInt("dev-eval-horizon");
    watchInt("dev-eval-tail");
    watchBool("dev-eval-require-deps");
    watchBool("dev-record-cands");
    watchInt("dev-eval-tick-budget");
    watchBool("dev-overlay");
    listenForSettingChanges<std::string>("dev-eval-grid", [](std::string) {
        onAnySetting();
    });
    listenForSettingChanges<std::string>("dev-objective", [](std::string) {
        onAnySetting();
    });
    watchBool("save-feedback-sound");
    watchBool("save-feedback-color");
    watchBool("save-feedback-counter");
    listenForSettingChanges<cocos2d::ccColor3B>("save-icon-color", [](cocos2d::ccColor3B) {
        onAnySetting();
    });
    auto watchFloat = [](char const* id) {
        listenForSettingChanges<double>(id, [](double) { onAnySetting(); });
    };
    char const* offFrom[] = {
        "assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
        "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"
    };
    char const* offTo[] = {
        "assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
        "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"
    };
    for (auto* id : offFrom) watchFloat(id);
    for (auto* id : offTo) watchFloat(id);

    char const* boolIds[] = {
        "cube-enable", "cube-overwrite",
        "ship-enable", "ship-overwrite",
        "ball-enable", "ball-overwrite",
        "ufo-enable", "ufo-overwrite",
        "wave-enable", "wave-overwrite",
        "robot-enable", "robot-overwrite",
        "spider-enable", "spider-overwrite",
        "swing-enable", "swing-overwrite"
    };
    for (auto* id : boolIds) watchBool(id);

    char const* intIds[] = {
        "cube-ticks", "cube-cap", "cube-slack",
        "ship-ticks", "ship-cap", "ship-slack",
        "ball-ticks", "ball-cap", "ball-slack",
        "ufo-ticks", "ufo-cap", "ufo-slack",
        "wave-ticks", "wave-cap", "wave-slack",
        "robot-ticks", "robot-cap", "robot-slack",
        "spider-ticks", "spider-cap", "spider-slack",
        "swing-ticks", "swing-cap", "swing-slack"
    };
    for (auto* id : intIds) watchInt(id);

    char const* latencyIds[] = {
        "cube-latency", "ship-latency", "ball-latency", "ufo-latency",
        "wave-latency", "robot-latency", "spider-latency", "swing-latency"
    };
    for (auto* id : latencyIds) watchFloat(id);
}
