#include "config/StageConfig.hpp"
#include "core/AssistController.hpp"
#include "debug/AssistLog.hpp"
#include "ui/StatusOverlay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;
using namespace frire;

namespace {

void onAnySetting() {
    if (StageConfig::isApplying()) return;
    StageConfig::refreshCache();
    AssistController::get().updateOverlay();
}

}

$on_mod(Loaded) {
    // Read saved values only — never re-apply a preset on load (that wiped custom ticks).
    StageConfig::refreshCache();
    AssistLog::printBanner();

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
    watchInt("ticks-wave");
    watchInt("death-slack");
    watchInt("look-ahead-cap");
    watchBool("debug-logging");
    watchBool("safe-mode");
    watchBool("swallow-hopeless");
    watchBool("auto-tune");
    watchInt("sim-budget-us");
    watchBool("assist-press");
    watchBool("assist-release");

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
}
