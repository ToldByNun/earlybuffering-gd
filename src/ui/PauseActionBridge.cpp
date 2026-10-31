#include "PauseActionBridge.hpp"
#include "InLevelSettingsFilter.hpp"

#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/BakeScheduler.hpp"
#include "../core/ConfigBundle.hpp"
#include "../core/ReplayImport.hpp"
#include "../core/SaveDropPaths.hpp"
#include "../debug/AssistLog.hpp"
#include "../config/StageConfig.hpp"
#include "../dev/DevConfig.hpp"
#include "../dev/DevDiagDump.hpp"
#include "../dev/DevReport.hpp"
#include "../dev/DevSession.hpp"
#include "../dev/DevTraceStore.hpp"
#include "../ui/DevOverlay.hpp"
#include "../ui/StatusOverlay.hpp"
#include "../ui/BakePathVisualizer.hpp"
#include "../ui/TrajectoryVisualizer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#ifdef GEODE_IS_WINDOWS
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <windows.h>
# include <commdlg.h>
#endif

using namespace geode::prelude;

// File-local helpers (class, not a namespace) for pickers / label lookup.
class PauseActionBridgeDetail {
public:
    static CCLabelBMFont* findStatusLabel(PauseLayer* self) {
        // Labels live under main-layer (NodeIDs) or PauseLayer root (legacy).
        return typeinfo_cast<CCLabelBMFont*>(
            self->getChildByIDRecursive("earlybuffer-bake-status"_spr)
        );
    }

    static CCLabelBMFont* findModeLabel(PauseLayer* self) {
        return typeinfo_cast<CCLabelBMFont*>(
            self->getChildByIDRecursive("earlybuffer-mode-label"_spr)
        );
    }

#ifdef GEODE_IS_WINDOWS
    static std::optional<std::filesystem::path> pickReplayFileWin32() {
        wchar_t fileBuf[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter =
            L"Replays (GDR/MHR/SLC)\0*.gdr;*.gdr2;*.mhr;*.json;*.slc\0"
            L"All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return std::nullopt;
        return std::filesystem::path(fileBuf);
    }

    static std::optional<std::filesystem::path> pickSaveJsonWin32(bool saveAs) {
        wchar_t fileBuf[MAX_PATH] = {};
        wcscpy_s(fileBuf, L"earlybuffer-config.json");
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"JSON\0*.json\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
            | (saveAs ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (saveAs) {
            if (!GetSaveFileNameW(&ofn)) return std::nullopt;
        }
        else {
            if (!GetOpenFileNameW(&ofn)) return std::nullopt;
        }
        return std::filesystem::path(fileBuf);
    }
#endif

    // Best-effort path copy. Failure still leaves the path in the alert body.
    static bool tryCopyPathToClipboard(std::filesystem::path const& path) {
        auto const s = path.string();
        if (s.empty()) return false;
        return clipboard::write(s);
    }

    static std::string pathClipboardNote(bool clipOk) {
        return clipOk
            ? "(Path copied to clipboard.)"
            : "(Could not copy path to clipboard — copy it from above.)";
    }

#ifndef GEODE_IS_WINDOWS
    // Neutral sandbox wording (Files / share / file manager / Finder-via-device).
    static char const* mobileTransferHint() {
        return "Use Files, Share, or a file manager "
               "(on Apple devices: Files app or Finder via the device).";
    }
#endif

    static std::string formatExportSuccess(std::filesystem::path const& path) {
        auto const st = ConfigBundle::lastExportStats();
        bool const clipOk = tryCopyPathToClipboard(path);
#ifndef GEODE_IS_WINDOWS
        return fmt::format(
            "Config exported (v{}).\n"
            "keys={}  skipped={}\n\n"
            "{}\n"
            "{}\n\n"
            "{}",
            st.version,
            st.written,
            st.skipped,
            path.string(),
            pathClipboardNote(clipOk),
            mobileTransferHint()
        );
#else
        return fmt::format(
            "Config exported (v{}).\n"
            "keys={}  skipped={}\n\n"
            "{}\n"
            "{}",
            st.version,
            st.written,
            st.skipped,
            path.string(),
            pathClipboardNote(clipOk)
        );
#endif
    }

    static std::string formatImportSuccess(std::optional<std::filesystem::path> const& path) {
        auto const st = ConfigBundle::lastImportStats();
        if (path) {
            bool const clipOk = tryCopyPathToClipboard(*path);
#ifndef GEODE_IS_WINDOWS
            return fmt::format(
                "Config imported.\n"
                "version={}  applied={}  skipped={}\n\n"
                "{}\n"
                "{}\n\n"
                "{}",
                st.version,
                st.applied,
                st.skipped,
                path->string(),
                pathClipboardNote(clipOk),
                mobileTransferHint()
            );
#else
            return fmt::format(
                "Config imported.\n"
                "version={}  applied={}  skipped={}\n\n"
                "{}\n"
                "{}",
                st.version,
                st.applied,
                st.skipped,
                path->string(),
                pathClipboardNote(clipOk)
            );
#endif
        }
        return fmt::format(
            "Config imported.\n"
            "version={}  applied={}  skipped={}",
            st.version,
            st.applied,
            st.skipped
        );
    }

#ifndef GEODE_IS_WINDOWS
    // Non-Win: pick newest replay from SaveDropPaths::replayDropDir().
    // nullopt = user was already alerted (ensureDirs fail / empty folder).
    static std::optional<std::filesystem::path> pickReplayFromDropDir() {
        auto& drops = SaveDropPaths::get();
        std::string err;
        if (!drops.ensureDirs(err)) {
            FLAlertLayer::create(
                "EarlyBuffer",
                err.empty()
                    ? "Could not create the replay drop folder."
                    : err.c_str(),
                "OK"
            )->show();
            return std::nullopt;
        }

        auto const dir = drops.replayDropDir();
        auto const candidates = drops.listReplayCandidates();
        if (candidates.empty()) {
            bool const clipOk = tryCopyPathToClipboard(dir);
            auto const msg = fmt::format(
                "No replay in the drop folder.\n\n"
                "{}\n"
                "{}\n\n"
                "{}\n\n"
                "Put one file there (.gdr .gdr2 .mhr .json .slc).\n"
                "Empty files are ignored.\n"
                "If several files are present, Import uses the newest.",
                dir.string(),
                pathClipboardNote(clipOk),
                mobileTransferHint()
            );
            FLAlertLayer::create("EarlyBuffer", msg.c_str(), "OK")->show();
            return std::nullopt;
        }

        auto const& chosen = candidates.front(); // newest mtime
        if (candidates.size() > 1) {
            AssistLog::info(fmt::format(
                "Import: {} file(s) in drop folder — using newest '{}'",
                candidates.size(),
                chosen.filename().string()
            ));
        }
        return chosen;
    }
#endif
};

void PauseActionBridge::refreshPauseLabels(PauseLayer* self) {
    if (!self) return;
    if (auto* mode = PauseActionBridgeDetail::findModeLabel(self)) {
        auto m = StageConfig::assistMode();
        mode->setString(fmt::format(
            "Mode: {} — {}",
            StageConfig::assistModeName(m),
            StageConfig::assistModeHint(m)
        ).c_str());
    }
    if (auto* st = PauseActionBridgeDetail::findStatusLabel(self)) {
        auto& bake = BakeMap::get();
        if (bake.isBaking() || BakeScheduler::get().running()) {
            st->setString(fmt::format(
                "{}\n{:.0f}%  ({}/{})",
                bake.progress().getStatus(),
                bake.progress().fraction() * 100.f,
                bake.progress().done.load(),
                bake.progress().total.load()
            ).c_str());
        }
        else if (DevSession::get().isRecording()) {
            st->setString(DevSession::get().statusLine().c_str());
        }
        else {
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
    auto const* r = BakeMap::get().pendingReplay();
    FLAlertLayer::create(
        "EarlyBuffer",
        fmt::format(
            "Imported {}\n{} events @ {:.0f} tps ({})\n\n"
            "Press Bake Map for replay windows.\n"
            "Brute Bake ignores this and searches the level itself.",
            r->sourceName, r->events.size(), r->tps, r->format
        ).c_str(),
        "OK"
    )->show();
    if (auto* pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
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
    if (!picked) return;
    handleImportedPath(*picked);
#else
    auto picked = PauseActionBridgeDetail::pickReplayFromDropDir();
    if (!picked) return;
    handleImportedPath(*picked);
#endif
}

void PauseActionBridge::startBake(bool brute) {
    auto* pl = PlayLayer::get();
    if (!pl) {
        FLAlertLayer::create("EarlyBuffer", "No level loaded.", "OK")->show();
        return;
    }
    if (BakeMap::get().isBaking() || BakeScheduler::get().running()) {
        FLAlertLayer::create("EarlyBuffer", "Bake already running.", "OK")->show();
        return;
    }
    if (!brute && !BakeMap::get().pendingReplay()) {
        FLAlertLayer::create(
            "EarlyBuffer",
            "Bake Map needs a replay.\nImport Replay first — or use Brute Bake\n(no macro, explores safe clicks).",
            "OK"
        )->show();
        return;
    }
    auto snap = AssistController::get().ensureStartSnapshot(pl);
    if (snap.isErr()) {
        FLAlertLayer::create(
            "EarlyBuffer",
            "Reset the level once (play from start),\nthen pause and bake again.",
            "OK"
        )->show();
        return;
    }

    int slot = AssistController::get().startSnapshotSlot();
    AssistLog::info(fmt::format("BAKE UI: starting {} (main-thread)", brute ? "BruteForce" : "Replay"));

    bool ok = BakeScheduler::get().start(
        pl, slot, StageConfig::bakeRadius(),
        brute ? BakeMethod::BruteForce : BakeMethod::Replay
    );
    if (!ok) {
        FLAlertLayer::create("EarlyBuffer", "Could not start bake.", "OK")->show();
        return;
    }
    AssistController::get().updateOverlay();
    if (auto* pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void PauseActionBridge::cycleAssistMode() {
    StageConfig::cycleAssistMode();
    auto mode = StageConfig::assistMode();
    AssistLog::info(fmt::format(
        "ASSIST MODE: {} — {}",
        StageConfig::assistModeName(mode),
        StageConfig::assistModeHint(mode)
    ));
    AssistController::get().updateOverlay();
    if (auto* pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
        refreshPauseLabels(pause);
    }
}

void PauseActionBridge::cancelBake() {
    if (!BakeMap::get().isBaking() && !BakeScheduler::get().running()) return;
    BakeScheduler::get().cancel();
    AssistLog::info("BAKE: cancel requested");
}

void PauseActionBridge::cyclePreset() {
    auto const stage = StageConfig::cycleNext();
    AssistLog::printCycle(stage, AssistController::get().estimatedTps());
    AssistController::get().updateOverlay();
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
    AssistLog::info(fmt::format("LEARN: smart learning {}", StageConfig::autoTune() ? "ON" : "OFF"));
    AssistController::get().updateOverlay();
}

void PauseActionBridge::toggleTrajectory() {
    StageConfig::toggleShowTrajectory();
    bool const on = StageConfig::showTrajectory();
    TrajectoryVisualizer::get().setEnabled(on);
    if (on) {
        auto* pl = PlayLayer::get();
        if (pl) TrajectoryVisualizer::get().attach(pl);
    }
    AssistLog::info(fmt::format("TRAJ: visualizer {}", on ? "ON" : "OFF"));
    AssistController::get().updateOverlay();
}

void PauseActionBridge::exportConfig() {
    std::optional<std::filesystem::path> path;
#ifdef GEODE_IS_WINDOWS
    path = PauseActionBridgeDetail::pickSaveJsonWin32(true);
    if (!path) return;
#else
    auto& drops = SaveDropPaths::get();
    std::string dirErr;
    if (!drops.ensureDirs(dirErr)) {
        FLAlertLayer::create(
            "EarlyBuffer",
            dirErr.empty() ? "Could not create the save folder." : dirErr.c_str(),
            "OK"
        )->show();
        return;
    }
    path = drops.configPath();
#endif
    auto r = ConfigBundle::exportTo(*path);
    if (r.isErr()) {
        auto err = r.unwrapErr();
        if (err.empty()) err = "Config export failed.";
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
#ifndef GEODE_IS_WINDOWS
        err = fmt::format(
            "{}\n\n{}\n{}\n\n{}",
            err,
            path->string(),
            PauseActionBridgeDetail::pathClipboardNote(clipOk),
            PauseActionBridgeDetail::mobileTransferHint()
        );
#else
        err = fmt::format(
            "{}\n\n{}\n{}",
            err,
            path->string(),
            PauseActionBridgeDetail::pathClipboardNote(clipOk)
        );
#endif
        FLAlertLayer::create("EarlyBuffer", err.c_str(), "OK")->show();
        return;
    }
    FLAlertLayer::create(
        "EarlyBuffer",
        PauseActionBridgeDetail::formatExportSuccess(*path).c_str(),
        "OK"
    )->show();
}

void PauseActionBridge::importConfig() {
    std::optional<std::filesystem::path> path;
#ifdef GEODE_IS_WINDOWS
    path = PauseActionBridgeDetail::pickSaveJsonWin32(false);
    if (!path) return;
#else
    auto& drops = SaveDropPaths::get();
    std::string dirErr;
    if (!drops.ensureDirs(dirErr)) {
        FLAlertLayer::create(
            "EarlyBuffer",
            dirErr.empty() ? "Could not create the save folder." : dirErr.c_str(),
            "OK"
        )->show();
        return;
    }
    path = drops.configPath();
    std::error_code ec;
    bool const exists = std::filesystem::is_regular_file(*path, ec) && !ec;
    if (!exists) {
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const msg = fmt::format(
            "No config file found.\n\n"
            "Place earlybuffer-config.json (full settings export) at:\n"
            "{}\n"
            "{}\n\n"
            "{}\n\n"
            "Export Config writes that path. Then Import again.",
            path->string(),
            PauseActionBridgeDetail::pathClipboardNote(clipOk),
            PauseActionBridgeDetail::mobileTransferHint()
        );
        FLAlertLayer::create("EarlyBuffer", msg.c_str(), "OK")->show();
        return;
    }
    auto const sz = std::filesystem::file_size(*path, ec);
    if (ec || sz == 0) {
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const emptyMsg = fmt::format(
            "Config file is empty or unreadable:\n"
            "{}\n"
            "{}",
            path->string(),
            PauseActionBridgeDetail::pathClipboardNote(clipOk)
        );
        FLAlertLayer::create("EarlyBuffer", emptyMsg.c_str(), "OK")->show();
        return;
    }
#endif
    auto r = ConfigBundle::importFrom(*path);
    if (r.isErr()) {
        bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(*path);
        auto const badMsg = fmt::format(
            "{}\n\nFile:\n{}\n{}",
            r.unwrapErr(),
            path->string(),
            PauseActionBridgeDetail::pathClipboardNote(clipOk)
        );
        FLAlertLayer::create("EarlyBuffer", badMsg.c_str(), "OK")->show();
        return;
    }

    // Import runs under applying-guard — sync UI that listeners would have skipped.
    StatusOverlay::setVisible(StageConfig::showOverlay());
    {
        bool const traj = StageConfig::showTrajectory();
        TrajectoryVisualizer::get().setEnabled(traj);
        if (traj) {
            if (auto* pl = PlayLayer::get()) {
                TrajectoryVisualizer::get().attach(pl);
            }
        }
        else {
            TrajectoryVisualizer::get().clear();
        }
        bool const bakePath = StageConfig::showBakePath();
        BakePathVisualizer::get().setEnabled(bakePath);
        if (bakePath) {
            if (auto* pl = PlayLayer::get()) {
                BakePathVisualizer::get().attach(pl);
            }
        }
        else {
            BakePathVisualizer::get().clear();
        }
    }
    AssistController::get().updateOverlay();
    // Open ModSettingsPopup rows keep stale uncommitted values after setSettingValue
    // → green labels / Apply would overwrite the import. Sync + rebuild UI.
    InLevelSettingsFilterKeeper::reloadAfterImport();
    auto const successMsg = PauseActionBridgeDetail::formatImportSuccess(path);
    // Show success after the settings rebuild so it stays on top.
    Loader::get()->queueInMainThread([successMsg] {
        FLAlertLayer::create("EarlyBuffer", successMsg.c_str(), "OK")->show();
    });
}

void PauseActionBridge::stubDevAction(char const* name) {
    AssistLog::info(fmt::format(
        "Dev Lab: '{}' is a stub (session/eval/trace not wired yet)",
        name ? name : "?"
    ));
}

void PauseActionBridge::devStartRecord() {
    if (!DevConfig::recordEnable()) {
        AssistLog::info("Dev Lab: enable Dev Record Enable before Start Recording");
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            "Turn on Dev Record Enable first, then Start Recording.",
            "OK"
        )->show();
        return;
    }
    if (!DevSession::get().start()) {
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            "Could not start Dev Lab recording session.",
            "OK"
        )->show();
        return;
    }
    auto const& s = DevTraceStore::get().session();
    auto const startMsg = fmt::format(
        "Recording started (rolling last {}).\nSession {}\nDoes not auto-stop.",
        s.recordingsTarget,
        s.sessionId
    );
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
    auto const stopMsg = path.empty()
        ? std::string("Recording stopped.")
        : fmt::format("Recording stopped.\n{}", path.string());
    FLAlertLayer::create("EarlyBuffer Dev", stopMsg.c_str(), "OK")->show();
}

void PauseActionBridge::devEvalLast() {
    std::string err;
    if (!DevReport::evalLastAttempt(err)) {
        AssistLog::info(fmt::format("Dev Lab: Eval Last failed: {}", err));
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            err.empty() ? "Eval Last Attempt failed." : err.c_str(),
            "OK"
        )->show();
        return;
    }
    AssistController::get().updateOverlay();
    auto const& r = DevReport::last();
    AssistLog::info(fmt::format("Dev Lab: Eval Last → {}", r.overlayLine));
    FLAlertLayer::create(
        "EarlyBuffer Dev",
        r.overlayLine.empty() ? "Eval complete." : r.overlayLine.c_str(),
        "OK"
    )->show();
}

void PauseActionBridge::devEvalSession() {
    std::string err;
    int count = 0;
    if (!DevReport::evalSession(err, &count)) {
        AssistLog::info(fmt::format("Dev Lab: Eval Session failed: {}", err));
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            err.empty() ? "Eval Session failed." : err.c_str(),
            "OK"
        )->show();
        return;
    }
    AssistController::get().updateOverlay();
    auto const& r = DevReport::last();
    auto const msg = fmt::format(
        "Evaluated {} attempt(s).\n{}",
        count,
        r.overlayLine
    );
    AssistLog::info(fmt::format("Dev Lab: Eval Session → {}", msg));
    FLAlertLayer::create("EarlyBuffer Dev", msg.c_str(), "OK")->show();
}

void PauseActionBridge::devExportSession() {
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) {
        AssistLog::info("Dev Lab: no open session to export");
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            "No open Dev session. Start Recording first.",
            "OK"
        )->show();
        return;
    }
    (void)store.flush();
    auto const path = store.sessionPath();
    AssistLog::info(fmt::format("Dev Lab: session export path {}", path.string()));
    bool const clipOk = PauseActionBridgeDetail::tryCopyPathToClipboard(path);
    auto const msg = fmt::format(
        "Session JSON:\n"
        "{}\n"
        "{}\n\n"
        "Attempts kept: {}/{} (rolling)",
        path.string(),
        PauseActionBridgeDetail::pathClipboardNote(clipOk),
        store.session().recordingsDone,
        store.session().recordingsTarget
    );
    FLAlertLayer::create("EarlyBuffer Dev", msg.c_str(), "OK")->show();
}

void PauseActionBridge::devClearTraces() {
    int const n = DevSession::get().clearTraces();
    auto const msg = fmt::format(
        "Cleared {} session file(s) under devtraces/.",
        n
    );
    FLAlertLayer::create("EarlyBuffer Dev", msg.c_str(), "OK")->show();
}

void PauseActionBridge::devDumpDiag() {
    std::filesystem::path dir;
    std::string err;
    if (!DevDiagDump::dumpAll(dir, err)) {
        AssistLog::info(fmt::format("DIAG dump failed: {}", err));
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            err.empty() ? "Diag dump failed." : err.c_str(),
            "OK"
        )->show();
        return;
    }
    bool const clipOk = clipboard::write(dir.string());
    auto const msg = fmt::format(
        "Wrote diagnostic bundle:\n{}\n\n"
        "Includes settings, clock, replay, BakeMap, Trigger World,\n"
        "bake probe (first clicks vs sim death), self-checks.\n\n"
        "{}",
        dir.string(),
        clipOk ? "Path copied to clipboard." : "Clipboard copy failed."
    );
    FLAlertLayer::create("EarlyBuffer Dev", msg.c_str(), "OK")->show();
}

void PauseActionBridge::devCopyReport() {
    if (!DevReport::hasLast()) {
        FLAlertLayer::create(
            "EarlyBuffer Dev",
            "No Dev report yet. Run Eval Last / Eval Session first.",
            "OK"
        )->show();
        return;
    }
    auto const& r = DevReport::last();
    // Clipboard / file = same overlay field string (contract A names only).
    std::string const text = r.overlayLine.empty()
        ? DevReport::formatOverlayLine(r)
        : r.overlayLine;

    auto& drops = SaveDropPaths::get();
    std::string dirErr;
    bool fileOk = false;
    std::string fileErr;
    auto const path = drops.reportPath();
    if (!drops.ensureDirs(dirErr)) {
        fileErr = dirErr.empty() ? "could not create save folder" : dirErr;
    }
    else {
        auto const written = file::writeString(path, text);
        if (written.isOk()) {
            fileOk = true;
        }
        else {
            fileErr = written.unwrapErr();
            if (fileErr.empty()) fileErr = "write failed";
        }
    }

    // Clipboard: report text + full path (do not overwrite report with path-only).
    std::string clipPayload = text;
    if (fileOk) {
        clipPayload += fmt::format("\n\n---\nSaved path:\n{}", path.string());
    }
    bool const clipOk = clipboard::write(clipPayload);

    AssistLog::info(fmt::format(
        "Dev Lab: copy report clip={} file={} → {}",
        clipOk ? "ok" : "fail",
        fileOk ? path.string() : fileErr,
        text
    ));

    std::string msg;
    if (clipOk && fileOk) {
        msg = fmt::format(
            "Report (+ path) copied to clipboard and saved to:\n"
            "{}\n"
            "{}\n\n"
            "{}",
            path.string(),
            PauseActionBridgeDetail::pathClipboardNote(true),
            text
        );
#ifndef GEODE_IS_WINDOWS
        msg += fmt::format("\n\n{}", PauseActionBridgeDetail::mobileTransferHint());
#endif
    }
    else if (fileOk && !clipOk) {
        msg = fmt::format(
            "Clipboard failed; saved to:\n"
            "{}\n"
            "{}\n\n"
            "{}",
            path.string(),
            PauseActionBridgeDetail::pathClipboardNote(false),
            text
        );
#ifndef GEODE_IS_WINDOWS
        msg += fmt::format("\n\n{}", PauseActionBridgeDetail::mobileTransferHint());
#endif
    }
    else if (clipOk && !fileOk) {
        msg = fmt::format(
            "Report copied to clipboard.\nFile save failed: {}\n\n{}",
            fileErr,
            text
        );
    }
    else {
        msg = fmt::format(
            "Copy failed (clipboard and file).\n"
            "File: {}\n\n"
            "{}",
            fileErr,
            text
        );
    }
    FLAlertLayer::create("EarlyBuffer Dev", msg.c_str(), "OK")->show();
}

void PauseActionBridge::openModSettings() {
    if (InLevelSettingsFilterKeeper::isOpen()) return;
    auto* mod = Mod::get();
    if (!mod) return;
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
