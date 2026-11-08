#include "pause_action_bridge.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/bake/bake_map.hpp"
#include "../../core/bake/bake_scheduler.hpp"
#include "../../core/bundle/config_bundle.hpp"
#include "../../core/replay/replay_import.hpp"
#include "../../debug/assist_log.hpp"
#include "../../dev/dev_session.hpp"
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
#endif

void PauseActionBridge::refreshPauseLabels(PauseLayer* self) {
    if (!self)
        return;
    if (auto* mode = PauseActionBridgeDetail::findModeLabel(self)) {
        auto m = StageConfig::assistMode();
        mode->setString(fmt::format("Mode: {} — {}", StageConfig::assistModeName(m),
                                    StageConfig::assistModeHint(m))
                            .c_str());
    }
    if (auto* st = PauseActionBridgeDetail::findStatusLabel(self)) {
        auto& bake = BakeMap::get();
        if (bake.isBaking() || BakeScheduler::get().running()) {
            st->setString(fmt::format("{}\n{:.0f}%  ({}/{})", bake.progress().getStatus(),
                                      bake.progress().fraction() * 100.f,
                                      bake.progress().done.load(), bake.progress().total.load())
                              .c_str());
        } else if (DevSession::get().isRecording()) {
            st->setString(DevSession::get().statusLine().c_str());
        } else {
            st->setString(bake.pauseStatusLine().c_str());
        }
    }
}

void PauseActionBridge::handleImportedPath(std::filesystem::path const& path) {
    auto loaded = ReplayImport::load(path);
    if (loaded.isErr()) {
        AssistLog::info(fmt::format("Import failed: {}", loaded.unwrapErr()));
        FLAlertLayer::create("EarlyBuffer", loaded.unwrapErr().c_str(), "OK")->show();
        return;
    }
    BakeMap::get().setPendingReplay(loaded.unwrap());
    AssistController::get().updateOverlay();
    auto const* result = BakeMap::get().pendingReplay();
    FLAlertLayer::create("EarlyBuffer",
                         fmt::format("Imported {}\n{} events @ {:.0f} tps ({})\n\n"
                                     "Press Bake Map for replay windows.\n"
                                     "Brute Bake ignores this and searches the level itself.",
                                     result->sourceName, result->events.size(), result->tps,
                                     result->format)
                             .c_str(),
                         "OK")
        ->show();
    if (auto* pause = geode::prelude::CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void PauseActionBridge::importReplay() {
    if (BakeMap::get().isBaking() || BakeScheduler::get().running()) {
        FLAlertLayer::create("EarlyBuffer", "Bake in progress — wait or cancel.", "OK")->show();
        return;
    }
#ifdef GEODE_IS_WINDOWS
    auto picked = PauseActionBridgeDetail::pickReplayFileWin32();
    if (!picked)
        return;
    handleImportedPath(*picked);
#else
    auto picked = PauseActionBridgeDetail::pickReplayFromDropDir();
    if (!picked)
        return;
    handleImportedPath(*picked);
#endif
}

void PauseActionBridge::startBake(bool brute) {
    auto* playLayer = PlayLayer::get();
    if (!playLayer) {
        FLAlertLayer::create("EarlyBuffer", "No level loaded.", "OK")->show();
        return;
    }
    if (BakeMap::get().isBaking() || BakeScheduler::get().running()) {
        FLAlertLayer::create("EarlyBuffer", "Bake already running.", "OK")->show();
        return;
    }
    if (!brute && !BakeMap::get().pendingReplay()) {
        FLAlertLayer::create("EarlyBuffer",
                             "Bake Map needs a replay.\nImport Replay first — or use Brute "
                             "Bake\n(no macro, explores safe clicks).",
                             "OK")
            ->show();
        return;
    }
    auto snap = AssistController::get().ensureStartSnapshot(playLayer);
    if (snap.isErr()) {
        FLAlertLayer::create("EarlyBuffer",
                             "Reset the level once (play from start),\nthen pause and bake again.",
                             "OK")
            ->show();
        return;
    }

    int slot = AssistController::get().startSnapshotSlot();
    AssistLog::info(
        fmt::format("BAKE UI: starting {} (main-thread)", brute ? "BruteForce" : "Replay"));

    bool ok = BakeScheduler::get().start(playLayer, slot, StageConfig::bakeRadius(),
                                         brute ? BakeMethod::BruteForce : BakeMethod::Replay);
    if (!ok) {
        FLAlertLayer::create("EarlyBuffer", "Could not start bake.", "OK")->show();
        return;
    }
    AssistController::get().updateOverlay();
    if (auto* pause = geode::prelude::CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void PauseActionBridge::cycleAssistMode() {
    StageConfig::cycleAssistMode();
    auto mode = StageConfig::assistMode();
    AssistLog::info(fmt::format("ASSIST MODE: {} — {}", StageConfig::assistModeName(mode),
                                StageConfig::assistModeHint(mode)));
    AssistController::get().updateOverlay();
    if (auto* pause = geode::prelude::CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void PauseActionBridge::cancelBake() {
    if (!BakeMap::get().isBaking() && !BakeScheduler::get().running())
        return;
    BakeScheduler::get().cancel();
    AssistLog::info("BAKE: cancel requested");
}

void PauseActionBridge::toggleDebug() {
    StageConfig::toggleDebugLogging();
    AssistLog::info(fmt::format("debug-logging: {}", StageConfig::debugLogging() ? "ON" : "OFF"));
}

void PauseActionBridge::resetLearning() {
    AssistController::get().resetLevelProfile();
    AssistLog::info("LEARN: level profile reset");
}

void PauseActionBridge::toggleAutoTune() {
    StageConfig::toggleAutoTune();
    AssistLog::info(
        fmt::format("LEARN: smart learning {}", StageConfig::autoTune() ? "ON" : "OFF"));
    AssistController::get().updateOverlay();
}

void PauseActionBridge::toggleTrajectory() {
    StageConfig::toggleShowTrajectory();
    bool const on = StageConfig::showTrajectory();
    TrajectoryVisualizer::get().setEnabled(on);
    if (on) {
        auto* playLayer = PlayLayer::get();
        if (playLayer)
            TrajectoryVisualizer::get().attach(playLayer);
    }
    AssistLog::info(fmt::format("TRAJ: visualizer {}", on ? "ON" : "OFF"));
    AssistController::get().updateOverlay();
}

void PauseActionBridge::exportConfig() {
    return PauseConfigActions::exportConfig();
}

void PauseActionBridge::importConfig() {
    return PauseConfigActions::importConfig();
}

void PauseActionBridge::openCloudConfigs() {
    return PauseConfigActions::openCloudConfigs();
}

void PauseActionBridge::afterConfigApplied(std::string const& successMsg) {
    return PauseConfigActions::afterConfigApplied(successMsg);
}

void PauseActionBridge::stubDevAction(char const* name) {
    return PauseDevActions::stubDevAction(name);
}

void PauseActionBridge::devStartRecord() {
    return PauseDevActions::devStartRecord();
}

void PauseActionBridge::devStopRecord() {
    return PauseDevActions::devStopRecord();
}

void PauseActionBridge::devEvalLast() {
    return PauseDevActions::devEvalLast();
}

void PauseActionBridge::devEvalSession() {
    return PauseDevActions::devEvalSession();
}

void PauseActionBridge::devExportSession() {
    return PauseDevActions::devExportSession();
}

void PauseActionBridge::devClearTraces() {
    return PauseDevActions::devClearTraces();
}

void PauseActionBridge::devDumpDiag() {
    return PauseDevActions::devDumpDiag();
}

void PauseActionBridge::devCopyReport() {
    return PauseDevActions::devCopyReport();
}

void PauseActionBridge::openModSettings() {
    if (InLevelSettingsFilterKeeper::isOpen())
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    auto* popup = geode::openSettingsPopup(mod, false);
    if (!popup) {
        AssistLog::info("openModSettings: no settings popup (mod has no settings?)");
        return;
    }
    InLevelSettingsFilterKeeper::attach(popup);
}

void PauseActionBridge::openHub() {
    openModSettings();
}

void PauseActionBridge::refreshOpenSettings() {
    InLevelSettingsFilterKeeper::refreshOpen();
}

auto PauseActionBridgeDetail::findStatusLabel(PauseLayer* self) -> geode::prelude::CCLabelBMFont* {
    // Labels live under main-layer (NodeIDs) or PauseLayer root (legacy).
    return geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(
        self->getChildByIDRecursive("earlybuffer-bake-status"_spr));
}

auto PauseActionBridgeDetail::findModeLabel(PauseLayer* self) -> geode::prelude::CCLabelBMFont* {
    return geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(
        self->getChildByIDRecursive("earlybuffer-mode-label"_spr));
}

auto PauseActionBridgeDetail::tryCopyPathToClipboard(std::filesystem::path const& path) -> bool {
    auto const text = path.string();
    if (!(text.empty())) {
        return geode::utils::clipboard::write(text);
    }
    return false;
}

auto PauseActionBridgeDetail::pathClipboardNote(bool clipOk) -> std::string {
    return clipOk ? "(Path copied to clipboard.)"
                  : "(Could not copy path to clipboard — copy it from above.)";
}

auto PauseActionBridgeDetail::formatExportSuccess(std::filesystem::path const& path)
    -> std::string {
    auto const state = ConfigBundle::lastExportStats();
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

auto PauseActionBridgeDetail::formatImportSuccess(std::optional<std::filesystem::path> const& path)
    -> std::string {
    auto const state = ConfigBundle::lastImportStats();
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
