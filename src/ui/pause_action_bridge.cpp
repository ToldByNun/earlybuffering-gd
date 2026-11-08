#include "pause_action_bridge.hpp"
#include "../config/stage_config.hpp"
#include "../core/assist_controller.hpp"
#include "../core/bake_map.hpp"
#include "../core/bake_scheduler.hpp"
#include "../core/config_bundle.hpp"
#include "../core/replay_import.hpp"
#include "../core/save_drop_paths.hpp"
#include "../debug/assist_log.hpp"
#include "../dev/dev_config.hpp"
#include "../dev/dev_diag_dump.hpp"
#include "../dev/dev_report.hpp"
#include "../dev/dev_session.hpp"
#include "../dev/dev_trace_store.hpp"
#include "../ui/bake_path_visualizer.hpp"
#include "../ui/dev_overlay.hpp"
#include "../ui/status_overlay.hpp"
#include "../ui/trajectory_visualizer.hpp"
#include "cloud_config_popup.hpp"
#include "in_level_settings_filter.hpp"
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

void PauseActionBridge::importConfig() {
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

void PauseActionBridge::openCloudConfigs() {
    CloudConfigPopup::open();
}

void PauseActionBridge::afterConfigApplied(std::string const& successMsg) {
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

void PauseActionBridge::stubDevAction(char const* name) {
    AssistLog::info(fmt::format("Dev Lab: '{}' is a stub (session/eval/trace not wired yet)",
                                name ? name : "?"));
}

void PauseActionBridge::devStartRecord() {
    if (!DevConfig::recordEnable()) {
        AssistLog::info("Dev Lab: enable Dev Record Enable before Start Recording");
        FLAlertLayer::create("EarlyBuffer Dev",
                             "Turn on Dev Record Enable first, then Start Recording.", "OK")
            ->show();
        return;
    }
    if (!DevSession::get().start()) {
        FLAlertLayer::create("EarlyBuffer Dev", "Could not start Dev Lab recording session.", "OK")
            ->show();
        return;
    }
    auto const& s = DevTraceStore::get().session();
    auto const startMsg =
        fmt::format("Recording started (rolling last {}).\nSession {}\nDoes not auto-stop.",
                    s.recordingsTarget, s.sessionId);
    FLAlertLayer::create("EarlyBuffer Dev", startMsg.c_str(), "OK")->show();
}

void PauseActionBridge::devStopRecord() {
    if (!DevSession::get().isRecording() && !DevTraceStore::get().isOpen()) {
        AssistLog::info("Dev Lab: no open session to stop");
        FLAlertLayer::create("EarlyBuffer Dev", "No session is recording.", "OK")->show();
        return;
    }
    auto const path = DevSession::get().sessionPath();
    DevSession::get().stop();
    auto const stopMsg = path.empty() ? std::string("Recording stopped.")
                                      : fmt::format("Recording stopped.\n{}", path.string());
    FLAlertLayer::create("EarlyBuffer Dev", stopMsg.c_str(), "OK")->show();
}

void PauseActionBridge::devEvalLast() {
    std::string err;
    if (!DevReport::evalLastAttempt(err)) {
        AssistLog::info(fmt::format("Dev Lab: Eval Last failed: {}", err));
        FLAlertLayer::create("EarlyBuffer Dev",
                             err.empty() ? "Eval Last Attempt failed." : err.c_str(), "OK")
            ->show();
        return;
    }
    AssistController::get().updateOverlay();
    auto const& r = DevReport::last();
    AssistLog::info(fmt::format("Dev Lab: Eval Last → {}", r.overlayLine));
    FLAlertLayer::create("EarlyBuffer Dev",
                         r.overlayLine.empty() ? "Eval complete." : r.overlayLine.c_str(), "OK")
        ->show();
}

void PauseActionBridge::devEvalSession() {
    std::string err;
    int count = 0;
    if (!DevReport::evalSession(err, &count)) {
        AssistLog::info(fmt::format("Dev Lab: Eval Session failed: {}", err));
        FLAlertLayer::create("EarlyBuffer Dev", err.empty() ? "Eval Session failed." : err.c_str(),
                             "OK")
            ->show();
        return;
    }
    AssistController::get().updateOverlay();
    auto const& r = DevReport::last();
    auto const message = fmt::format("Evaluated {} attempt(s).\n{}", count, r.overlayLine);
    AssistLog::info(fmt::format("Dev Lab: Eval Session → {}", message));
    FLAlertLayer::create("EarlyBuffer Dev", message.c_str(), "OK")->show();
}

void PauseActionBridge::devExportSession() {
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) {
        AssistLog::info("Dev Lab: no open session to export");
        FLAlertLayer::create("EarlyBuffer Dev", "No open Dev session. Start Recording first.", "OK")
            ->show();
        return;
    }
    (void)store.flush();
    auto const path = store.sessionPath();
    AssistLog::info(fmt::format("Dev Lab: session export path {}", path.string()));
    bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(path);
    auto const message =
        fmt::format("Session JSON:\n"
                    "{}\n"
                    "{}\n\n"
                    "Attempts kept: {}/{} (rolling)",
                    path.string(), PauseActionBridgeDetail::pathClipboardNote(clipOk),
                    store.session().recordingsDone, store.session().recordingsTarget);
    FLAlertLayer::create("EarlyBuffer Dev", message.c_str(), "OK")->show();
}

void PauseActionBridge::devClearTraces() {
    int const count = DevSession::get().clearTraces();
    auto const message = fmt::format("Cleared {} session file(s) under devtraces/.", count);
    FLAlertLayer::create("EarlyBuffer Dev", message.c_str(), "OK")->show();
}

void PauseActionBridge::devDumpDiag() {
    std::filesystem::path dir;
    std::string err;
    if (!DevDiagDump::dumpAll(dir, err)) {
        AssistLog::info(fmt::format("DIAG dump failed: {}", err));
        FLAlertLayer::create("EarlyBuffer Dev", err.empty() ? "Diag dump failed." : err.c_str(),
                             "OK")
            ->show();
        return;
    }
    bool const clipOk = geode::utils::clipboard::write(dir.string());
    auto const message =
        fmt::format("Wrote diagnostic bundle:\n{}\n\n"
                    "Includes settings, clock, replay, BakeMap, Trigger World,\n"
                    "bake probe (first clicks vs sim death), self-checks.\n\n"
                    "{}",
                    dir.string(), clipOk ? "Path copied to clipboard." : "Clipboard copy failed.");
    FLAlertLayer::create("EarlyBuffer Dev", message.c_str(), "OK")->show();
}

void PauseActionBridge::devCopyReport() {
    if (!DevReport::hasLast()) {
        FLAlertLayer::create("EarlyBuffer Dev",
                             "No Dev report yet. Run Eval Last / Eval Session first.", "OK")
            ->show();
        return;
    }
    auto const& r = DevReport::last();
    // Clipboard / file = same overlay field string (contract A names only).
    std::string const text =
        r.overlayLine.empty() ? DevReport::formatOverlayLine(r) : r.overlayLine;

    auto& drops = SaveDropPaths::get();
    std::string dirErr;
    bool fileOk = false;
    std::string fileErr;
    auto const path = drops.reportPath();
    if (!drops.ensureDirs(dirErr)) {
        fileErr = dirErr.empty() ? "could not create save folder" : dirErr;
    } else {
        auto const written = geode::utils::file::writeString(path, text);
        if (written.isOk()) {
            fileOk = true;
        } else {
            fileErr = written.unwrapErr();
            if (fileErr.empty())
                fileErr = "write failed";
        }
    }

    // Clipboard: report text + full path (do not overwrite report with path-only).
    std::string clipPayload = text;
    if (fileOk) {
        clipPayload += fmt::format("\n\n---\nSaved path:\n{}", path.string());
    }
    bool const clipOk = geode::utils::clipboard::write(clipPayload);

    AssistLog::info(fmt::format("Dev Lab: copy report clip={} file={} → {}", clipOk ? "ok" : "fail",
                                fileOk ? path.string() : fileErr, text));

    std::string msg;
    if (clipOk && fileOk) {
        msg = fmt::format("Report (+ path) copied to clipboard and saved to:\n"
                          "{}\n"
                          "{}\n\n"
                          "{}",
                          path.string(), PauseActionBridgeDetail::pathClipboardNote(true), text);
#ifndef GEODE_IS_WINDOWS
        msg += fmt::format("\n\n{}", PauseActionBridgeDetail::mobileTransferHint());
#endif
    } else if (fileOk && !clipOk) {
        msg = fmt::format("Clipboard failed; saved to:\n"
                          "{}\n"
                          "{}\n\n"
                          "{}",
                          path.string(), PauseActionBridgeDetail::pathClipboardNote(false), text);
#ifndef GEODE_IS_WINDOWS
        msg += fmt::format("\n\n{}", PauseActionBridgeDetail::mobileTransferHint());
#endif
    } else if (clipOk && !fileOk) {
        msg = fmt::format("Report copied to clipboard.\nFile save failed: {}\n\n{}", fileErr, text);
    } else {
        msg = fmt::format("Copy failed (clipboard and file).\n"
                          "File: {}\n\n"
                          "{}",
                          fileErr, text);
    }
    FLAlertLayer::create("EarlyBuffer Dev", msg.c_str(), "OK")->show();
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
