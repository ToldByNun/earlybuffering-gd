#include "pause_action_bridge.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/bake/bake_map.hpp"
#include "../../core/bake/bake_scheduler.hpp"
#include "../../core/bundle/config_bundle.hpp"
#include "../../core/replay/replay_import.hpp"
#include "../../debug/assist_log.hpp"
#include "../../dev/dev_session.hpp"
#include "../desktop_menu/desktop_menu.hpp"
#include "../settings/filtering/in_level_settings_filter_keeper.hpp"
#include "../visualizers/trajectory_visualizer.hpp"
#include "actions/pause_config_actions.hpp"
#include "actions/pause_dev_actions.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#ifdef GEODE_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commdlg.h>
#include <windows.h>
#include "../desktop_menu/desktop_menu_alert.hpp"
#endif

void EbPauseActionBridge::refreshPauseLabels(PauseLayer* self) {
    if (!self)
        return;
    if (auto* mode = EbPauseActionBridgeDetail::findModeLabel(self)) {
        auto m = EbStageConfig::assistMode();
        mode->setString(fmt::format("Mode: {} — {}", EbStageConfig::assistModeName(m),
                                    EbStageConfig::assistModeHint(m))
                            .c_str());
    }
    if (auto* st = EbPauseActionBridgeDetail::findStatusLabel(self)) {
        auto& bake = EbBakeMap::get();
        if (bake.isBaking() || EbBakeScheduler::get().running()) {
            st->setString(fmt::format("{}\n{:.0f}%  ({}/{})", bake.progress().getStatus(),
                                      bake.progress().fraction() * 100.f,
                                      bake.progress().done.load(), bake.progress().total.load())
                              .c_str());
        } else if (EbDevSession::get().isRecording()) {
            st->setString(EbDevSession::get().statusLine().c_str());
        } else {
            st->setString(bake.pauseStatusLine().c_str());
        }
    }
}

void EbPauseActionBridge::handleImportedPath(std::filesystem::path const& path) {
    auto loaded = EbReplayImport::load(path);
    if (loaded.isErr()) {
        EbAssistLog::info(fmt::format("Import failed: {}", loaded.unwrapErr()));
        EbDesktopAlert::info("EarlyBuffer", loaded.unwrapErr().c_str());
        return;
    }
    EbBakeMap::get().setPendingReplay(loaded.unwrap());
    EbAssistController::get().updateOverlay();
    auto const* result = EbBakeMap::get().pendingReplay();
    EbDesktopAlert::info(
        "EarlyBuffer",
        fmt::format("Imported {}\n{} events @ {:.0f} tps ({})\n\n"
                    "Press Bake Map for replay windows.\n"
                    "Brute Bake ignores this and searches the level itself.",
                    result->sourceName, result->events.size(), result->tps, result->format));
    if (auto* pause = geode::prelude::CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void EbPauseActionBridge::importReplay() {
    if (EbBakeMap::get().isBaking() || EbBakeScheduler::get().running()) {
        EbDesktopAlert::info("EarlyBuffer", "Bake in progress — wait or cancel.");
        return;
    }
#ifdef GEODE_IS_WINDOWS
    auto picked = EbPauseActionBridgeDetail::pickReplayFileWin32();
    if (!picked)
        return;
    handleImportedPath(*picked);
#else
    auto picked = EbPauseActionBridgeDetail::pickReplayFromDropDir();
    if (!picked)
        return;
    handleImportedPath(*picked);
#endif
}

void EbPauseActionBridge::startBake(bool brute) {
    auto* playLayer = PlayLayer::get();
    if (!playLayer) {
        EbDesktopAlert::info("EarlyBuffer", "No level loaded.");
        return;
    }
    if (EbBakeMap::get().isBaking() || EbBakeScheduler::get().running()) {
        EbDesktopAlert::info("EarlyBuffer", "Bake already running.");
        return;
    }
    if (!brute && !EbBakeMap::get().pendingReplay()) {
        EbDesktopAlert::info("EarlyBuffer", "Bake Map needs a replay.\nImport Replay first — or use Brute "
                             "Bake\n(no macro, explores safe clicks).");
        return;
    }
    auto snap = EbAssistController::get().ensureStartSnapshot(playLayer);
    if (snap.isErr()) {
        EbDesktopAlert::info("EarlyBuffer", "Reset the level once (play from start),\nthen pause and bake again.");
        return;
    }

    int slot = EbAssistController::get().startSnapshotSlot();
    EbAssistLog::info(
        fmt::format("BAKE UI: starting {} (main-thread)", brute ? "BruteForce" : "Replay"));

    bool ok = EbBakeScheduler::get().start(playLayer, slot, EbStageConfig::bakeRadius(),
                                         brute ? BakeMethod::BruteForce : BakeMethod::Replay);
    if (!ok) {
        EbDesktopAlert::info("EarlyBuffer", "Could not start bake.");
        return;
    }
    EbAssistController::get().updateOverlay();
    if (auto* pause = geode::prelude::CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void EbPauseActionBridge::cycleAssistMode() {
    EbStageConfig::cycleAssistMode();
    auto mode = EbStageConfig::assistMode();
    EbAssistLog::info(fmt::format("ASSIST MODE: {} — {}", EbStageConfig::assistModeName(mode),
                                EbStageConfig::assistModeHint(mode)));
    EbAssistController::get().updateOverlay();
    if (auto* pause = geode::prelude::CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void EbPauseActionBridge::cancelBake() {
    if (!EbBakeMap::get().isBaking() && !EbBakeScheduler::get().running())
        return;
    EbBakeScheduler::get().cancel();
    EbAssistLog::info("BAKE: cancel requested");
}

void EbPauseActionBridge::toggleDebug() {
    EbStageConfig::toggleDebugLogging();
    EbAssistLog::info(fmt::format("debug-logging: {}", EbStageConfig::debugLogging() ? "ON" : "OFF"));
}

void EbPauseActionBridge::resetLearning() {
    EbAssistController::get().resetLevelProfile();
    EbAssistLog::info("LEARN: level profile reset");
}

void EbPauseActionBridge::toggleAutoTune() {
    EbStageConfig::toggleAutoTune();
    EbAssistLog::info(
        fmt::format("LEARN: smart learning {}", EbStageConfig::autoTune() ? "ON" : "OFF"));
    EbAssistController::get().updateOverlay();
}

void EbPauseActionBridge::toggleTrajectory() {
    EbStageConfig::toggleShowTrajectory();
    bool const on = EbStageConfig::showTrajectory();
    EbTrajectoryVisualizer::get().setEnabled(on);
    if (on) {
        auto* playLayer = PlayLayer::get();
        if (playLayer)
            EbTrajectoryVisualizer::get().attach(playLayer);
    }
    EbAssistLog::info(fmt::format("TRAJ: visualizer {}", on ? "ON" : "OFF"));
    EbAssistController::get().updateOverlay();
}

void EbPauseActionBridge::exportConfig() {
    return EbPauseConfigActions::exportConfig();
}

void EbPauseActionBridge::importConfig() {
    return EbPauseConfigActions::importConfig();
}

void EbPauseActionBridge::openCloudConfigs() {
    return EbPauseConfigActions::openCloudConfigs();
}

void EbPauseActionBridge::afterConfigApplied(std::string const& successMsg) {
    return EbPauseConfigActions::afterConfigApplied(successMsg);
}

void EbPauseActionBridge::stubDevAction(char const* name) {
    return EbPauseDevActions::stubDevAction(name);
}

void EbPauseActionBridge::devStartRecord() {
    return EbPauseDevActions::devStartRecord();
}

void EbPauseActionBridge::devStopRecord() {
    return EbPauseDevActions::devStopRecord();
}

void EbPauseActionBridge::devEvalLast() {
    return EbPauseDevActions::devEvalLast();
}

void EbPauseActionBridge::devEvalSession() {
    return EbPauseDevActions::devEvalSession();
}

void EbPauseActionBridge::devExportSession() {
    return EbPauseDevActions::devExportSession();
}

void EbPauseActionBridge::devClearTraces() {
    return EbPauseDevActions::devClearTraces();
}

void EbPauseActionBridge::devDumpDiag() {
    return EbPauseDevActions::devDumpDiag();
}

void EbPauseActionBridge::devCopyReport() {
    return EbPauseDevActions::devCopyReport();
}

void EbPauseActionBridge::devExportResearchPack() {
    return EbPauseDevActions::devExportResearchPack();
}

void EbPauseActionBridge::devCompareSessions() {
    return EbPauseDevActions::devCompareSessions();
}

void EbPauseActionBridge::openModSettings() {
    if (EbDesktopMenu::useImGuiShell()) {
        // ImGui XOR classic: do not stack over an open Geode popup.
        if (EbInLevelSettingsFilterKeeper::isOpen())
            return;
        if (!EbDesktopMenu::isOpen())
            EbDesktopMenu::setOpen(true);
        return;
    }
    if (EbDesktopMenu::isOpen())
        EbDesktopMenu::setOpen(false);
    if (EbInLevelSettingsFilterKeeper::isOpen())
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    auto* popup = geode::openSettingsPopup(mod, false);
    if (!popup) {
        EbAssistLog::info("openModSettings: no settings popup (mod has no settings?)");
        return;
    }
    EbInLevelSettingsFilterKeeper::attach(popup);
}

void EbPauseActionBridge::openHub() {
    openModSettings();
}

void EbPauseActionBridge::refreshOpenSettings() {
    EbInLevelSettingsFilterKeeper::refreshOpen();
}

auto EbPauseActionBridgeDetail::findStatusLabel(PauseLayer* self) -> geode::prelude::CCLabelBMFont* {
    // Labels live under main-layer (NodeIDs) or PauseLayer root (legacy).
    return geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(
        self->getChildByIDRecursive("earlybuffer-bake-status"_spr));
}

auto EbPauseActionBridgeDetail::findModeLabel(PauseLayer* self) -> geode::prelude::CCLabelBMFont* {
    return geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(
        self->getChildByIDRecursive("earlybuffer-mode-label"_spr));
}

auto EbPauseActionBridgeDetail::tryCopyPathToClipboard(std::filesystem::path const& path) -> bool {
    auto const text = path.string();
    if (!(text.empty())) {
        return geode::utils::clipboard::write(text);
    }
    return false;
}

auto EbPauseActionBridgeDetail::pathClipboardNote(bool clipOk) -> std::string {
    return clipOk ? "(Path copied to clipboard.)"
                  : "(Could not copy path to clipboard — copy it from above.)";
}

auto EbPauseActionBridgeDetail::formatExportSuccess(std::filesystem::path const& path)
    -> std::string {
    auto const state = EbConfigBundle::lastExportStats();
    bool const clipOk = tryCopyPathToClipboard(path);
#ifndef GEODE_IS_WINDOWS
    return fmt::format("Config exported (v{}).\n"
                       "keys={}  skipped={}\n\n"
                       "{}\n"
                       "{}\n\n"
                       "{}",
                       state.version, state.written, state.skipped, path.string(),
                       pathClipboardNote(clipOk), mobileTransferHint());
#else
    return fmt::format("Config exported (v{}).\n"
                       "keys={}  skipped={}\n\n"
                       "{}\n"
                       "{}",
                       state.version, state.written, state.skipped, path.string(),
                       pathClipboardNote(clipOk));
#endif
}

auto EbPauseActionBridgeDetail::formatImportSuccess(std::optional<std::filesystem::path> const& path)
    -> std::string {
    auto const state = EbConfigBundle::lastImportStats();
    if (path) {
        bool const clipOk = tryCopyPathToClipboard(*path);
#ifndef GEODE_IS_WINDOWS
        return fmt::format("Config imported.\n"
                           "version={}  applied={}  skipped={}\n\n"
                           "{}\n"
                           "{}\n\n"
                           "{}",
                           state.version, state.applied, state.skipped, path->string(),
                           pathClipboardNote(clipOk), mobileTransferHint());
#else
        return fmt::format("Config imported.\n"
                           "version={}  applied={}  skipped={}\n\n"
                           "{}\n"
                           "{}",
                           state.version, state.applied, state.skipped, path->string(),
                           pathClipboardNote(clipOk));
#endif
    }
    return fmt::format("Config imported.\n"
                       "version={}  applied={}  skipped={}",
                       state.version, state.applied, state.skipped);
}
