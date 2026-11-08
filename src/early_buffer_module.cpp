#include "early_buffer_module.hpp"
#include "config/stage_config.hpp"
#include "core/assist/assist_controller.hpp"
#include "core/assist/contracts/assist_contracts140.hpp"
#include "core/bake/bake_map.hpp"
#include "core/platform/save_drop_paths.hpp"
#include "core/sim/shadow_simulator.hpp"
#include "core/trigger_world/trigger_classify.hpp"
#include "core/trigger_world/trigger_world_store.hpp"
#include "debug/assist_log.hpp"
#include "debug/lag_probe.hpp"
#include "debug/perf_trace.hpp"
#include "debug/plan_gold_corpus.hpp"
#include "dev/dev_config.hpp"
#include "ui/overlays/status_overlay.hpp"
#include "ui/settings/action_button_setting.hpp"
#include "ui/settings/in_level_settings_filter.hpp"
#include "ui/visualizers/bake_path_visualizer.hpp"
#include "ui/visualizers/trajectory_visualizer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

void EarlyBufferModule::syncPerfTrace() {
    PerfTrace::get().setEnabled(StageConfig::perfTrace());
}

void EarlyBufferModule::syncLagProbe() {
    LagProbe::get().setEnabled(StageConfig::lagProbe());
}

void EarlyBufferModule::onAnySetting() {
    if (StageConfig::isApplying())
        return;
    StageConfig::refreshCache();
    DevConfig::refreshCache();
    EarlyBufferModule::syncPerfTrace();
    EarlyBufferModule::syncLagProbe();
    AssistController::get().updateOverlay();
}

void EarlyBufferModule::onLoaded() {
    // Custom eb-action rows (Bake/Assist buttons) — must resolve before settings UI opens.
    ActionButtonSettingImplementation::registerActionButtonSettingType();
    // Tabs for every EarlyBuffer settings open (Geode menu + pause EB).
    InLevelSettingsFilterKeeper::startWatchdog();

    StageConfig::refreshCache();
    DevConfig::refreshCache();
    EarlyBufferModule::syncPerfTrace();
    EarlyBufferModule::syncLagProbe();
    // Mobile drop folders (replay-drop / config) — soft-fail if save dir is read-only.
    {
        std::string dropErr;
        if (!SaveDropPaths::get().ensureDirs(dropErr) && !dropErr.empty()) {
            AssistLog::info(fmt::format("WARN SaveDropPaths: {}", dropErr));
        }
    }
    AssistLog::printBanner();
    // Commit 2 LOCK: freeze GhostParityProbe ε before any runtime / PlanGold probe.
    AssistContracts140::applyLockedEps();
    AssistLog::info(fmt::format("AssistContracts140 LOCK schema={} corpus={} eps=({:.0f},{:.0f})",
                                AssistContracts140::kSchema, AssistContracts140::kCorpusLabel,
                                AssistContracts140::kLockedEpsX, AssistContracts140::kLockedEpsY));
    if (StageConfig::perfTrace()) {
        AssistLog::info(fmt::format("PerfTrace logging to {}", PerfTrace::logPath()));
    }
    if (StageConfig::lagProbe()) {
        AssistLog::info(fmt::format("LagProbe logging to {}", LagProbe::logPath()));
    }

    {
        std::string report;
        bool const ok = ShadowSimulator::regressionSelfCheck(report);
        if (ok)
            AssistLog::info(report);
        else
            AssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = TriggerClassify::selfCheck(report);
        if (ok)
            AssistLog::info(report);
        else
            AssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = TriggerWorldStore::selfCheck(report);
        if (ok)
            AssistLog::info(report);
        else
            AssistLog::info(fmt::format("WARN {}", report));
    }
    {
        std::string report;
        bool const ok = PlanGoldCorpus::run(report);
        if (ok)
            AssistLog::info(report);
        else
            AssistLog::info(fmt::format("WARN {}", report));
    }

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-debug", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat)
                return;
            StageConfig::toggleDebugLogging();
            bool const now = StageConfig::debugLogging();
            AssistLog::info(fmt::format("debug-logging: {}", now ? "ON" : "OFF"));
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-reset-level", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat)
                return;
            AssistController::get().resetLevelProfile();
            AssistLog::info("LEARN: level profile reset (Ctrl+Shift+R)");
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-auto-tune", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat)
                return;
            StageConfig::toggleAutoTune();
            bool const now = StageConfig::autoTune();
            AssistLog::info(fmt::format("LEARN: smart learning {}", now ? "ON" : "OFF"));
            AssistController::get().updateOverlay();
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-assist-mode", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat)
                return;
            StageConfig::cycleAssistMode();
            auto mode = StageConfig::assistMode();
            AssistLog::info(fmt::format("ASSIST MODE: {} — {}", StageConfig::assistModeName(mode),
                                        StageConfig::assistModeHint(mode)));
            AssistController::get().updateOverlay();
        });

    geode::prelude::listenForKeybindSettingPresses(
        "hotkey-trajectory", [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat)
                return;
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

    geode::prelude::listenForSettingChanges<bool>("show-overlay", [](bool value) {
        if (StageConfig::isApplying())
            return;
        StageConfig::refreshCache();
        StatusOverlay::setVisible(value);
        AssistController::get().updateOverlay();
    });

    auto watchBool = [](char const* id) {
        geode::prelude::listenForSettingChanges<bool>(
            id, [](bool) { EarlyBufferModule::onAnySetting(); });
    };
    auto watchInt = [](char const* id) {
        geode::prelude::listenForSettingChanges<int64_t>(
            id, [](int64_t) { EarlyBufferModule::onAnySetting(); });
    };

    watchBool("enabled");
    watchInt("ticks-all");
    geode::prelude::listenForSettingChanges<std::string>(
        "window-unit", [](std::string) { EarlyBufferModule::onAnySetting(); });
    watchInt("ticks-wave");
    watchInt("death-slack");
    watchInt("look-ahead-cap");
    watchBool("debug-logging");
    watchBool("perf-trace");
    watchBool("lag-probe");
    watchBool("safe-mode");

    geode::prelude::listenForSettingChanges<bool>("show-trajectory", [](bool value) {
        if (StageConfig::isApplying())
            return;
        StageConfig::refreshCache();
        TrajectoryVisualizer::get().setEnabled(value);
        if (value) {
            if (auto* pl = PlayLayer::get())
                TrajectoryVisualizer::get().attach(pl);
        } else {
            TrajectoryVisualizer::get().clear();
        }
        AssistController::get().updateOverlay();
    });
    geode::prelude::listenForSettingChanges<bool>("show-bake-path", [](bool value) {
        if (StageConfig::isApplying())
            return;
        StageConfig::refreshCache();
        BakePathVisualizer::get().setEnabled(value);
        if (value) {
            if (auto* pl = PlayLayer::get())
                BakePathVisualizer::get().attach(pl);
        } else {
            BakePathVisualizer::get().clear();
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

    geode::prelude::listenForSettingChanges<std::string>(
        "assist-mode", [](std::string) { EarlyBufferModule::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-pick", [](std::string) { EarlyBufferModule::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-engine", [](std::string) { EarlyBufferModule::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "assist-verify", [](std::string) { EarlyBufferModule::onAnySetting(); });

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
        "dev-eval-grid", [](std::string) { EarlyBufferModule::onAnySetting(); });
    geode::prelude::listenForSettingChanges<std::string>(
        "dev-objective", [](std::string) { EarlyBufferModule::onAnySetting(); });
    watchBool("save-feedback-sound");
    watchBool("save-feedback-color");
    watchBool("save-feedback-counter");
    geode::prelude::listenForSettingChanges<cocos2d::ccColor3B>(
        "save-icon-color", [](cocos2d::ccColor3B) { EarlyBufferModule::onAnySetting(); });
    auto watchFloat = [](char const* id) {
        geode::prelude::listenForSettingChanges<double>(
            id, [](double) { EarlyBufferModule::onAnySetting(); });
    };
    char const* offFrom[] = {"assist-off-1-from", "assist-off-2-from", "assist-off-3-from",
                             "assist-off-4-from", "assist-off-5-from", "assist-off-6-from"};
    char const* offTo[] = {"assist-off-1-to", "assist-off-2-to", "assist-off-3-to",
                           "assist-off-4-to", "assist-off-5-to", "assist-off-6-to"};
    for (auto* id : offFrom)
        watchFloat(id);
    for (auto* id : offTo)
        watchFloat(id);

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

$on_mod(Loaded) {
    EarlyBufferModule::onLoaded();
}
