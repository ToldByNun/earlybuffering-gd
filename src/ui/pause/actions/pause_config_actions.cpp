#include "pause_config_actions.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../core/assist/assist_controller.hpp"
#include "../../../core/bundle/config_bundle.hpp"
#include "../../../core/platform/save_drop_paths.hpp"
#include "../../cloud/cloud_config_popup.hpp"
#include "../../overlays/status_overlay.hpp"
#include "../../settings/filtering/in_level_settings_filter_keeper.hpp"
#include "../../visualizers/bake_path_visualizer.hpp"
#include "../../visualizers/trajectory_visualizer.hpp"
#include "../pause_action_bridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <commdlg.h>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <windows.h>

void PauseConfigActions::exportConfig() {
    std::optional<std::filesystem::path> path;
#ifdef GEODE_IS_WINDOWS
    path = PauseActionBridgeDetail::pickSaveJsonWin32(true);
    if (!path)
        return;
#else
    auto& drops = SaveDropPaths::get();
    std::string dirErr;
    if (!drops.ensureDirs(dirErr)) {
        FLAlertLayer::create("EarlyBuffer",
                             dirErr.empty() ? "Could not create the save folder." : dirErr.c_str(),
                             "OK")
            ->show();
        return;
    }
    path = drops.configPath();
#endif
    auto result = ConfigBundle::exportTo(*path);
    if (result.isErr()) {
        auto errorMessage = result.unwrapErr();
        if (errorMessage.empty())
            errorMessage = "Config export failed.";
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
#ifndef GEODE_IS_WINDOWS
        errorMessage = fmt::format("{}\n\n{}\n{}\n\n{}", errorMessage, path->string(),
                                   PauseActionBridgeDetail::pathClipboardNote(clipOk),
                                   PauseActionBridgeDetail::mobileTransferHint());
#else
        errorMessage = fmt::format("{}\n\n{}\n{}", errorMessage, path->string(),
                                   PauseActionBridgeDetail::pathClipboardNote(clipOk));
#endif
        FLAlertLayer::create("EarlyBuffer", errorMessage.c_str(), "OK")->show();
        return;
    }
    FLAlertLayer::create("EarlyBuffer", PauseActionBridgeDetail::formatExportSuccess(*path).c_str(),
                         "OK")
        ->show();
}

void PauseConfigActions::importConfig() {
    std::optional<std::filesystem::path> path;
#ifdef GEODE_IS_WINDOWS
    path = PauseActionBridgeDetail::pickSaveJsonWin32(false);
    if (!path)
        return;
#else
    auto& drops = SaveDropPaths::get();
    std::string dirErr;
    if (!drops.ensureDirs(dirErr)) {
        FLAlertLayer::create("EarlyBuffer",
                             dirErr.empty() ? "Could not create the save folder." : dirErr.c_str(),
                             "OK")
            ->show();
        return;
    }
    path = drops.configPath();
    std::error_code ec;
    bool const exists = std::filesystem::is_regular_file(*path, ec) && !ec;
    if (!exists) {
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const message =
            fmt::format("No config file found.\n\n"
                        "Place earlybuffer-config.json (full settings export) at:\n"
                        "{}\n"
                        "{}\n\n"
                        "{}\n\n"
                        "Export Config writes that path. Then Import again.",
                        path->string(), PauseActionBridgeDetail::pathClipboardNote(clipOk),
                        PauseActionBridgeDetail::mobileTransferHint());
        FLAlertLayer::create("EarlyBuffer", message.c_str(), "OK")->show();
        return;
    }
    auto const size = std::filesystem::file_size(*path, ec);
    if (ec || size == 0) {
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const emptyMsg =
            fmt::format("Config file is empty or unreadable:\n"
                        "{}\n"
                        "{}",
                        path->string(), PauseActionBridgeDetail::pathClipboardNote(clipOk));
        FLAlertLayer::create("EarlyBuffer", emptyMsg.c_str(), "OK")->show();
        return;
    }
#endif
    auto result = ConfigBundle::importFrom(*path);
    if (result.isErr()) {
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const badMsg = fmt::format("{}\n\nFile:\n{}\n{}", result.unwrapErr(), path->string(),
                                        PauseActionBridgeDetail::pathClipboardNote(clipOk));
        FLAlertLayer::create("EarlyBuffer", badMsg.c_str(), "OK")->show();
        return;
    }

    PauseActionBridge::afterConfigApplied(PauseActionBridgeDetail::formatImportSuccess(path));
}

void PauseConfigActions::openCloudConfigs() {
    CloudConfigPopup::open();
}

void PauseConfigActions::afterConfigApplied(std::string const& successMsg) {
    // Import runs under applying-guard — sync UI that listeners would have skipped.
    StatusOverlay::setVisible(StageConfig::showOverlay());
    {
        bool const traj = StageConfig::showTrajectory();
        TrajectoryVisualizer::get().setEnabled(traj);
        if (traj) {
            if (auto* pl = PlayLayer::get()) {
                TrajectoryVisualizer::get().attach(pl);
            }
        } else {
            TrajectoryVisualizer::get().clear();
        }
        bool const bakePath = StageConfig::showBakePath();
        BakePathVisualizer::get().setEnabled(bakePath);
        if (bakePath) {
            if (auto* pl = PlayLayer::get()) {
                BakePathVisualizer::get().attach(pl);
            }
        } else {
            BakePathVisualizer::get().clear();
        }
    }
    AssistController::get().updateOverlay();
    // Open ModSettingsPopup rows keep stale uncommitted values after setSettingValue
    // → green labels / Apply would overwrite the import. Sync + rebuild UI.
    InLevelSettingsFilterKeeper::reloadAfterImport();
    // Show success after the settings rebuild so it stays on top.
    geode::prelude::Loader::get()->queueInMainThread(
        [successMsg] { FLAlertLayer::create("EarlyBuffer", successMsg.c_str(), "OK")->show(); });
}
