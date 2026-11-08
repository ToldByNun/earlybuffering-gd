#include "pause_dev_actions.hpp"
#include "../../../core/assist/assist_controller.hpp"
#include "../../../core/platform/save_drop_paths.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../dev/capture/dev_trace_store.hpp"
#include "../../../dev/dev_config.hpp"
#include "../../../dev/dev_session.hpp"
#include "../../../dev/reporting/dev_diag_dump.hpp"
#include "../../../dev/reporting/dev_report.hpp"
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

void PauseDevActions::stubDevAction(char const* name) {
    AssistLog::info(fmt::format("Dev Lab: '{}' is a stub (session/eval/trace not wired yet)",
                                name ? name : "?"));
}

void PauseDevActions::devStartRecord() {
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

void PauseDevActions::devStopRecord() {
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

void PauseDevActions::devEvalLast() {
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

void PauseDevActions::devEvalSession() {
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

void PauseDevActions::devExportSession() {
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

void PauseDevActions::devClearTraces() {
    int const count = DevSession::get().clearTraces();
    auto const message = fmt::format("Cleared {} session file(s) under devtraces/.", count);
    FLAlertLayer::create("EarlyBuffer Dev", message.c_str(), "OK")->show();
}

void PauseDevActions::devDumpDiag() {
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

void PauseDevActions::devCopyReport() {
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
