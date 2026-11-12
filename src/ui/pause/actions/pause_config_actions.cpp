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
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include "../../desktop_menu/desktop_menu_alert.hpp"

void EbPauseConfigActions::exportConfig() {
    std::optional<std::filesystem::path> path;
#ifdef GEODE_IS_WINDOWS
    path = EbPauseActionBridgeDetail::pickSaveJsonWin32(true);
    if (!path)
        return;
#else
    auto& drops = EbSaveDropPaths::get();
    std::string dirErr;
    if (!drops.ensureDirs(dirErr)) {
        EbDesktopAlert::info("EarlyBuffer", dirErr.empty() ? "Could not create the save folder." : dirErr.c_str());
        return;
    }
    path = drops.configPath();
#endif
    auto result = EbConfigBundle::exportTo(*path);
    if (result.isErr()) {
        auto errorMessage = result.unwrapErr();
        if (errorMessage.empty())
            errorMessage = "Config export failed.";
        bool const clipOk = EbPauseActionBridgeDetail::tryCopyPathToClipboard(*path);
#ifndef GEODE_IS_WINDOWS
        errorMessage = fmt::format("{}\n\n{}\n{}\n\n{}", errorMessage, path->string(),
                                   EbPauseActionBridgeDetail::pathClipboardNote(clipOk),
                                   EbPauseActionBridgeDetail::mobileTransferHint());
#else
        errorMessage = fmt::format("{}\n\n{}\n{}", errorMessage, path->string(),
                                   EbPauseActionBridgeDetail::pathClipboardNote(clipOk));
#endif
        EbDesktopAlert::info("EarlyBuffer", errorMessage.c_str());
        return;
    }
    EbDesktopAlert::info("EarlyBuffer", EbPauseActionBridgeDetail::formatExportSuccess(*path).c_str());
}

void EbPauseConfigActions::importConfig() {
    std::optional<std::filesystem::path> path;
#ifdef GEODE_IS_WINDOWS
    path = EbPauseActionBridgeDetail::pickSaveJsonWin32(false);
    if (!path)
        return;
#else
    auto& drops = EbSaveDropPaths::get();
    std::string dirErr;
    if (!drops.ensureDirs(dirErr)) {
        EbDesktopAlert::info("EarlyBuffer", dirErr.empty() ? "Could not create the save folder." : dirErr.c_str());
        return;
    }
    path = drops.configPath();
    std::error_code ec;
    bool const exists = std::filesystem::is_regular_file(*path, ec) && !ec;
    if (!exists) {
        bool const clipOk = EbPauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const message =
            fmt::format("No config file found.\n\n"
                        "Place earlybuffer-config.json (full settings export) at:\n"
                        "{}\n"
                        "{}\n\n"
                        "{}\n\n"
                        "Export Config writes that path. Then Import again.",
                        path->string(), EbPauseActionBridgeDetail::pathClipboardNote(clipOk),
                        EbPauseActionBridgeDetail::mobileTransferHint());
        EbDesktopAlert::info("EarlyBuffer", message.c_str());
        return;
    }
    auto const size = std::filesystem::file_size(*path, ec);
    if (ec || size == 0) {
        bool const clipOk = EbPauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const emptyMsg =
            fmt::format("Config file is empty or unreadable:\n"
                        "{}\n"
                        "{}",
                        path->string(), EbPauseActionBridgeDetail::pathClipboardNote(clipOk));
        EbDesktopAlert::info("EarlyBuffer", emptyMsg.c_str());
        return;
    }
#endif
    auto result = EbConfigBundle::importFrom(*path);
    if (result.isErr()) {
        bool const clipOk = EbPauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const badMsg = fmt::format("{}\n\nFile:\n{}\n{}", result.unwrapErr(), path->string(),
                                        EbPauseActionBridgeDetail::pathClipboardNote(clipOk));
        EbDesktopAlert::info("EarlyBuffer", badMsg.c_str());
        return;
    }

    EbPauseActionBridge::afterConfigApplied(EbPauseActionBridgeDetail::formatImportSuccess(path));
}

void EbPauseConfigActions::openCloudConfigs() {
    EbCloudConfigPopup::open();
}

void EbPauseConfigActions::afterConfigApplied(std::string const& successMsg) {
    // Import runs under applying-guard — sync UI that listeners would have skipped.
    EbStatusOverlay::setVisible(EbStageConfig::showOverlay());
    {
        bool const traj = EbStageConfig::showTrajectory();
        EbTrajectoryVisualizer::get().setEnabled(traj);
        if (traj) {
            if (auto* pl = PlayLayer::get()) {
                EbTrajectoryVisualizer::get().attach(pl);
            }
        } else {
            EbTrajectoryVisualizer::get().clear();
        }
        bool const bakePath = EbStageConfig::showBakePath();
        EbBakePathVisualizer::get().setEnabled(bakePath);
        if (bakePath) {
            if (auto* pl = PlayLayer::get()) {
                EbBakePathVisualizer::get().attach(pl);
            }
        } else {
            EbBakePathVisualizer::get().clear();
        }
    }
    EbAssistController::get().updateOverlay();
    // Open ModSettingsPopup rows keep stale uncommitted values after setSettingValue
    // → green labels / Apply would overwrite the import. Sync + rebuild UI.
    EbInLevelSettingsFilterKeeper::reloadAfterImport();
    // Show success after the settings rebuild so it stays on top.
    geode::prelude::Loader::get()->queueInMainThread(
        [successMsg] { EbDesktopAlert::info("EarlyBuffer", successMsg.c_str()); });
}
