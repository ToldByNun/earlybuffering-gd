#include "module_bindings.hpp"
#include "class_manager.hpp"

#include "../config/stage_config.hpp"
#include "../core/assist/assist_controller.hpp"
#include "../core/assist/contracts/assist_contracts140.hpp"
#include "../core/bake/bake_map.hpp"
#include "../core/platform/save_drop_paths.hpp"
#include "../core/sim/shadow_simulator.hpp"
#include "../core/trigger_world/trigger_classify.hpp"
#include "../core/trigger_world/trigger_world_store.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../debug/plan_gold_corpus.hpp"
#include "../dev/dev_config.hpp"
#include "../ui/desktop_menu/desktop_menu.hpp"
#include "../ui/desktop_menu/widgets/eb_keybind.hpp"
#include "../ui/overlays/status_overlay.hpp"
#include "../ui/pause/pause_action_bridge.hpp"
#include "../ui/settings/action_button_setting.hpp"
#include "../ui/settings/in_level_settings_filter.hpp"
#include "../ui/visualizers/bake_path_visualizer.hpp"
#include "../ui/visualizers/trajectory_visualizer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

void EbModuleBindings::onAnySetting() {
    EbClassManager::get().refreshSettings();
}

void EbModuleBindings::registerSettings() {
    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-debug", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || EbKeybind::isCapturing())
                return;
            EbStageConfig::toggleDebugLogging();
            bool const now = EbStageConfig::debugLogging();
            EbAssistLog::info(fmt::format("debug-logging: {}", now ? "ON" : "OFF"));
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-reset-level", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || EbKeybind::isCapturing())
                return;
            EbAssistController::get().resetLevelProfile();
            EbAssistLog::info("LEARN: level profile reset (Ctrl+Shift+R)");
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-auto-tune", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || EbKeybind::isCapturing())
                return;
            EbStageConfig::toggleAutoTune();
            bool const now = EbStageConfig::autoTune();
            EbAssistLog::info(fmt::format("LEARN: smart learning {}", now ? "ON" : "OFF"));
            EbAssistController::get().updateOverlay();
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-assist-mode", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || EbKeybind::isCapturing())
                return;
            EbStageConfig::cycleAssistMode();
            auto mode = EbStageConfig::assistMode();
            EbAssistLog::info(fmt::format("ASSIST MODE: {} — {}", EbStageConfig::assistModeName(mode),
                                        EbStageConfig::assistModeHint(mode)));
            EbAssistController::get().updateOverlay();
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-trajectory", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || EbKeybind::isCapturing())
                return;
            EbStageConfig::toggleShowTrajectory();
            bool const on = EbStageConfig::showTrajectory();
            EbTrajectoryVisualizer::get().setEnabled(on);
            if (on) {
                if (auto* pl = PlayLayer::get()) {
                    EbTrajectoryVisualizer::get().attach(pl);
                }
            }
            EbAssistLog::info(fmt::format("TRAJ: visualizer {}", on ? "ON" : "OFF"));
            EbAssistController::get().updateOverlay();
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-desktop-menu", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat)
                return;
            // Never leave the menu stuck open because a keybind row is capturing.
            if (EbKeybind::isCapturing()) {
                EbKeybind::cancelCapture();
                return;
            }
            if (EbDesktopMenu::useImGuiShell()) {
                EbDesktopMenu::toggle();
                return;
            }
            // Classic preference or mobile: open Geode settings popup.
            EbPauseActionBridge::openModSettings();
        });

    geode::prelude::listenForSettingChanges<bool>(
        "desktop-classic-settings", [](bool classic) {
            if (classic && EbDesktopMenu::isOpen())
                EbDesktopMenu::setOpen(false);
            if (!EbStageConfig::isApplying())
                EbModuleBindings::onAnySetting();
        });

    geode::prelude::listenForSettingChanges<bool>("show-overlay", [](bool value) {
        if (EbStageConfig::isApplying())
            return;
        EbStageConfig::refreshCache();
        EbStatusOverlay::setVisible(value);
        EbAssistController::get().updateOverlay();
    });

    auto watchBool = [](char const* id) {
        geode::prelude::listenForSettingChanges<bool>(
            id, [](bool) { EbModuleBindings::onAnySetting(); });
    };
    auto watchInt = [](char const* id) {
        geode::prelude::listenForSettingChanges<int64_t>(
            id, [](int64_t) { EbModuleBindings::onAnySetting(); });
    };

    watchBool("enabled");
    watchInt("ticks-all");
    geode::prelude::listenForSettingChanges<std::string>(
        "window-unit", [](std::string) { EbModuleBindings::onAnySetting(); });
    watchInt("ticks-wave");
    watchInt("death-slack");
    watchInt("look-ahead-cap");
    watchBool("debug-logging");
    watchBool("perf-trace");
    watchBool("lag-probe");
    watchBool("safe-mode");

    geode::prelude::listenForSettingChanges<bool>("show-trajectory", [](bool value) {
        if (EbStageConfig::isApplying())
            return;
        EbStageConfig::refreshCache();
        EbTrajectoryVisualizer::get().setEnabled(value);
        if (value) {
            if (auto* pl = PlayLayer::get())
                EbTrajectoryVisualizer::get().attach(pl);
        } else {
            EbTrajectoryVisualizer::get().clear();
        }
        EbAssistController::get().updateOverlay();
    });
    geode::prelude::listenForSettingChanges<bool>("show-bake-path", [](bool value) {
        if (EbStageConfig::isApplying())
            return;
        EbStageConfig::refreshCache();
        EbBakePathVisualizer::get().setEnabled(value);
        if (value) {
            if (auto* pl = PlayLayer::get())
                EbBakePathVisualizer::get().attach(pl);
        } else {
            EbBakePathVisualizer::get().clear();
        }
        EbAssistController::get().updateOverlay();
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
    watchBool("adaptive-plan");
    watchInt("adaptive-screen-ticks");
    watchInt("adaptive-verify-quantum");
    watchInt("adaptive-reserve-pct");

    geode::prelude::listenForSettingChanges<std::string>(
        "assist-mode", [](std::string) { EbModuleBindings::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-pick", [](std::string) { EbModuleBindings::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-engine", [](std::string) { EbModuleBindings::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-verify", [](std::string) { EbModuleBindings::onAnySetting(); });

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
    geode::prelude::listenForSettingChanges<std::string>(
        "dev-eval-grid", [](std::string) { EbModuleBindings::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "dev-objective", [](std::string) { EbModuleBindings::onAnySetting(); });
    watchBool("save-feedback-sound");
    watchBool("save-feedback-color");
    watchBool("save-feedback-counter");
    geode::prelude::listenForSettingChanges<cocos2d::ccColor3B>(
        "save-icon-color", [](cocos2d::ccColor3B) { EbModuleBindings::onAnySetting(); });
    geode::prelude::listenForSettingChanges<cocos2d::ccColor3B>(
        "desktop-accent-color", [](cocos2d::ccColor3B) { EbModuleBindings::onAnySetting(); });
    auto watchFloat = [](char const* id) {
        geode::prelude::listenForSettingChanges<double>(
            id, [](double) { EbModuleBindings::onAnySetting(); });
    };
    auto watchAssistOffFloat = [](char const* id) {
        geode::prelude::listenForSettingChanges<double>(id, [](double) {
            // Classic Geode rows are source of truth until desktop rewrites JSON.
            EbStageConfig::rebuildAssistOffRangesFromLegacySlots();
            EbModuleBindings::onAnySetting();
        });
    };
    char const* offFrom[] = {"assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
                             "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"};
    char const* offTo[] = {"assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
                           "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"};
    for (auto* id : offFrom)
        watchAssistOffFloat(id);
    for (auto* id : offTo)
        watchAssistOffFloat(id);
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-off-ranges", [](std::string) { EbModuleBindings::onAnySetting(); });

    char const* boolIds[] = {
        "cube-enable",   "cube-overwrite",   "ship-enable",  "ship-overwrite",
        "ball-enable",   "ball-overwrite",   "ufo-enable",   "ufo-overwrite",
        "wave-enable",   "wave-overwrite",   "robot-enable", "robot-overwrite",
        "spider-enable", "spider-overwrite", "swing-enable", "swing-overwrite"};
    for (auto* id : boolIds)
        watchBool(id);

    char const* intIds[] = {
        "cube-ticks",   "cube-cap",   "cube-slack",   "ship-ticks",  "ship-cap",  "ship-slack",
        "ball-ticks",   "ball-cap",   "ball-slack",   "ufo-ticks",   "ufo-cap",   "ufo-slack",
        "wave-ticks",   "wave-cap",   "wave-slack",   "robot-ticks", "robot-cap", "robot-slack",
        "spider-ticks", "spider-cap", "spider-slack", "swing-ticks", "swing-cap", "swing-slack"};
    for (auto* id : intIds)
        watchInt(id);

    char const* latencyIds[] = {"cube-latency", "ship-latency",  "ball-latency",   "ufo-latency",
                                "wave-latency", "robot-latency", "spider-latency", "swing-latency"};
    for (auto* id : latencyIds)
        watchFloat(id);

}
