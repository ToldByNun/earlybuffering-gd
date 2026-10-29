#include "PauseActionBridge.hpp"
#include "InLevelSettingsFilter.hpp"

#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/BakeScheduler.hpp"
#include "../core/ConfigBundle.hpp"
#include "../core/ReplayImport.hpp"
#include "../debug/AssistLog.hpp"
#include "../config/StageConfig.hpp"
#include "../dev/DevConfig.hpp"
#include "../dev/DevReport.hpp"
#include "../dev/DevSession.hpp"
#include "../dev/DevTraceStore.hpp"
#include "../ui/DevOverlay.hpp"
#include "../ui/TrajectoryVisualizer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <optional>
#include <string>

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
    FLAlertLayer::create(
        "EarlyBuffer",
        "Replay import is Windows-only for now.",
        "OK"
    )->show();
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
#ifdef GEODE_IS_WINDOWS
    auto path = PauseActionBridgeDetail::pickSaveJsonWin32(true);
    if (!path) return;
    auto r = ConfigBundle::exportTo(*path);
    if (r.isErr()) FLAlertLayer::create("EarlyBuffer", r.unwrapErr().c_str(), "OK")->show();
    else FLAlertLayer::create("EarlyBuffer", "Config exported.", "OK")->show();
#else
    auto path = Mod::get()->getSaveDir() / "earlybuffer-config.json";
    auto r = ConfigBundle::exportTo(path);
    FLAlertLayer::create(
        "EarlyBuffer",
        r.isOk() ? fmt::format("Exported to {}", path.string()).c_str() : r.unwrapErr().c_str(),
        "OK"
    )->show();
#endif
}

void PauseActionBridge::importConfig() {
#ifdef GEODE_IS_WINDOWS
    auto path = PauseActionBridgeDetail::pickSaveJsonWin32(false);
    if (!path) return;
    auto r = ConfigBundle::importFrom(*path);
#else
    auto path = Mod::get()->getSaveDir() / "earlybuffer-config.json";
    auto r = ConfigBundle::importFrom(path);
#endif
    if (r.isErr()) {
        FLAlertLayer::create("EarlyBuffer", r.unwrapErr().c_str(), "OK")->show();
        return;
    }
    AssistController::get().updateOverlay();
    FLAlertLayer::create("EarlyBuffer", "Config imported.", "OK")->show();
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
    auto const msg = fmt::format(
        "Session JSON:\n{}\nAttempts kept: {}/{} (rolling)",
        path.string(),
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
    // Clipboard = same overlay field string (contract A names only).
    std::string const text = r.overlayLine.empty()
        ? DevReport::formatOverlayLine(r)
        : r.overlayLine;
    clipboard::write(text);
    AssistLog::info(fmt::format("Dev Lab: copied report → {}", text));
    FLAlertLayer::create(
        "EarlyBuffer Dev",
        fmt::format("Copied to clipboard:\n{}", text).c_str(),
        "OK"
    )->show();
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
