#include "pause_dev_actions.hpp"
#include "../../../core/assist/assist_controller.hpp"
#include "../../../core/platform/save_drop_paths.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../dev/capture/dev_trace_store.hpp"
#include "../../../dev/dev_config.hpp"
#include "../../../dev/dev_session.hpp"
#include "../../../dev/reporting/dev_diag_dump.hpp"
#include "../../../dev/reporting/dev_report.hpp"
#include "../../../dev/reporting/dev_research_pack.hpp"
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

void EbPauseDevActions::stubDevAction(char const* name) {
    EbAssistLog::info(fmt::format("Dev Lab: '{}' is a stub (session/eval/trace not wired yet)",
                                name ? name : "?"));
}

void EbPauseDevActions::devStartRecord() {
    if (!EbDevConfig::recordEnable()) {
        EbAssistLog::info("Dev Lab: enable Dev Record Enable before Start Recording");
        EbDesktopAlert::info("EarlyBuffer Dev", "Turn on Dev Record Enable first, then Start Recording.");
        return;
    }
    if (!EbDevSession::get().start()) {
        EbDesktopAlert::info("EarlyBuffer Dev", "Could not start Dev Lab recording session.");
        return;
    }
    auto const& s = EbDevTraceStore::get().session();
    auto const startMsg =
        fmt::format("Recording started (rolling last {}).\nSession {}\nDoes not auto-stop.",
                    s.recordingsTarget, s.sessionId);
    EbDesktopAlert::info("EarlyBuffer Dev", startMsg.c_str());
}

void EbPauseDevActions::devStopRecord() {
    if (!EbDevSession::get().isRecording() && !EbDevTraceStore::get().isOpen()) {
        EbAssistLog::info("Dev Lab: no open session to stop");
        EbDesktopAlert::info("EarlyBuffer Dev", "No session is recording.");
        return;
    }
    auto const path = EbDevSession::get().sessionPath();
    EbDevSession::get().stop();
    auto const stopMsg = path.empty() ? std::string("Recording stopped.")
                                      : fmt::format("Recording stopped.\n{}", path.string());
    EbDesktopAlert::info("EarlyBuffer Dev", stopMsg.c_str());
}

void EbPauseDevActions::devEvalLast() {
    std::string err;
    if (!EbDevReport::evalLastAttempt(err)) {
        EbAssistLog::info(fmt::format("Dev Lab: Eval Last failed: {}", err));
        EbDesktopAlert::info("EarlyBuffer Dev", err.empty() ? "Eval Last Attempt failed." : err.c_str());
        return;
    }
    EbAssistController::get().updateOverlay();
    auto const& r = EbDevReport::last();
    EbAssistLog::info(fmt::format("Dev Lab: Eval Last → {}", r.overlayLine));
    EbDesktopAlert::info("EarlyBuffer Dev", r.overlayLine.empty() ? "Eval complete." : r.overlayLine.c_str());
}

void EbPauseDevActions::devEvalSession() {
    std::string err;
    int count = 0;
    if (!EbDevReport::evalSession(err, &count)) {
        EbAssistLog::info(fmt::format("Dev Lab: Eval Session failed: {}", err));
        EbDesktopAlert::info("EarlyBuffer Dev", err.empty() ? "Eval Session failed." : err.c_str());
        return;
    }
    EbAssistController::get().updateOverlay();
    auto const& r = EbDevReport::last();
    auto const message = fmt::format("Evaluated {} attempt(s).\n{}", count, r.overlayLine);
    EbAssistLog::info(fmt::format("Dev Lab: Eval Session → {}", message));
    EbDesktopAlert::info("EarlyBuffer Dev", message.c_str());
}

void EbPauseDevActions::devExportSession() {
    auto& store = EbDevTraceStore::get();
    if (!store.isOpen()) {
        EbAssistLog::info("Dev Lab: no open session to export");
        EbDesktopAlert::info("EarlyBuffer Dev", "No open Dev session. Start Recording first.");
        return;
    }
    (void)store.flush();
    auto const path = store.sessionPath();
    EbAssistLog::info(fmt::format("Dev Lab: session export path {}", path.string()));
    bool const clipOk = EbPauseActionBridgeDetail::tryCopyPathToClipboard(path);
    auto const message =
        fmt::format("Session JSON:\n"
                    "{}\n"
                    "{}\n\n"
                    "Attempts kept: {}/{} (rolling)",
                    path.string(), EbPauseActionBridgeDetail::pathClipboardNote(clipOk),
                    store.session().recordingsDone, store.session().recordingsTarget);
    EbDesktopAlert::info("EarlyBuffer Dev", message.c_str());
}

void EbPauseDevActions::devClearTraces() {
    EbDesktopAlert::confirm(
        "Clear Dev Traces",
        "Stop recording (if active) and delete all session-*.json under devtraces/?\n"
        "This cannot be undone.",
        []() {
            int const count = EbDevSession::get().clearTraces();
            auto const message =
                fmt::format("Cleared {} session file(s) under devtraces/.", count);
            EbDesktopAlert::info("EarlyBuffer Dev", message.c_str());
        },
        {}, "Clear", "Cancel");
}

void EbPauseDevActions::devDumpDiag() {
    std::filesystem::path dir;
    std::string err;
    if (!EbDevDiagDump::dumpAll(dir, err)) {
        EbAssistLog::info(fmt::format("DIAG dump failed: {}", err));
        EbDesktopAlert::info("EarlyBuffer Dev", err.empty() ? "Diag dump failed." : err.c_str());
        return;
    }
    bool const clipOk = geode::utils::clipboard::write(dir.string());
    auto const message =
        fmt::format("Wrote diagnostic bundle:\n{}\n\n"
                    "Includes settings, clock, replay, BakeMap, Trigger World,\n"
                    "bake probe (first clicks vs sim death), self-checks.\n\n"
                    "{}",
                    dir.string(), clipOk ? "Path copied to clipboard." : "Clipboard copy failed.");
    EbDesktopAlert::info("EarlyBuffer Dev", message.c_str());
}

void EbPauseDevActions::devExportResearchPack() {
    // One click: eval everything we can → seal pack → clipboard = pack folder.
    auto& store = EbDevTraceStore::get();
    if (store.isOpen())
        (void)store.flush();

    int evalCount = 0;
    std::string evalNote;
    std::string evalErr;
    if (store.isOpen()) {
        if (EbDevReport::evalSession(evalErr, &evalCount)) {
            evalNote = fmt::format("Eval Session: {} attempt(s).", evalCount);
            EbAssistController::get().updateOverlay();
        } else if (EbDevReport::evalLastAttempt(evalErr)) {
            evalCount = 1;
            evalNote = "Eval Last (session-wide eval failed).";
            EbAssistController::get().updateOverlay();
        } else {
            evalNote = fmt::format("Eval skipped: {}",
                                   evalErr.empty() ? "no planFinished witness" : evalErr);
        }
    } else if (EbDevReport::evalLastAttempt(evalErr)) {
        evalCount = 1;
        evalNote = "Eval Last (no open session).";
        EbAssistController::get().updateOverlay();
    } else {
        evalNote = fmt::format("Eval skipped: {}",
                               evalErr.empty() ? "no open session / no witness" : evalErr);
    }

    EbDevResearchPack::Result pack;
    std::string err;
    if (!EbDevResearchPack::exportPack(pack, err)) {
        EbAssistLog::info(fmt::format("Dev Lab: research pack failed: {}", err));
        EbDesktopAlert::info(
            "EarlyBuffer Dev",
            fmt::format("{}\n\nPack failed: {}", evalNote,
                        err.empty() ? "export failed" : err)
                .c_str());
        return;
    }

    bool const clipOk = geode::utils::clipboard::write(pack.dir.string());
    auto const message = fmt::format(
        "{}\n\n"
        "Research pack ready — paste this folder path:\n{}\n\n"
        "session={}  report={}  config={}  files={}\n"
        "{}",
        evalNote, pack.dir.string(), pack.hasSession ? "yes" : "no",
        pack.hasReport ? "yes" : "no", pack.hasConfig ? "yes" : "no", pack.files.size(),
        clipOk ? "Folder path copied to clipboard." : "Clipboard copy failed — copy the path above.");
    EbAssistLog::info(fmt::format("Dev Lab: research pack → {} ({})", pack.dir.string(), evalNote));
    EbDesktopAlert::info("EarlyBuffer Dev", message.c_str());
}

void EbPauseDevActions::devCopyReport() {
    if (!EbDevReport::hasLast()) {
        EbDesktopAlert::info("EarlyBuffer Dev", "No Dev report yet. Run Eval Last / Eval Session first.");
        return;
    }
    auto const& r = EbDevReport::last();
    // Clipboard / file = same overlay field string (contract A names only).
    std::string const text =
        r.overlayLine.empty() ? EbDevReport::formatOverlayLine(r) : r.overlayLine;

    auto& drops = EbSaveDropPaths::get();
    std::string dirErr;
    bool fileOk = false;
    std::string fileErr;
    auto const path = drops.reportPath();
    auto const jsonPath = drops.rootDir() / "earlybuffer-dev-report.json";
    if (!drops.ensureDirs(dirErr)) {
        fileErr = dirErr.empty() ? "could not create save folder" : dirErr;
    } else {
        auto const written = geode::utils::file::writeString(path, text);
        if (written.isOk()) {
            fileOk = true;
            if (!r.jsonLine.empty()) {
                (void)geode::utils::file::writeString(jsonPath, r.jsonLine + "\n");
            }
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
        if (!r.jsonLine.empty()) {
            clipPayload += fmt::format("\nJSON:\n{}", jsonPath.string());
        }
    }
    bool const clipOk = geode::utils::clipboard::write(clipPayload);

    EbAssistLog::info(fmt::format("Dev Lab: copy report clip={} file={} → {}", clipOk ? "ok" : "fail",
                                fileOk ? path.string() : fileErr, text));

    std::string msg;
    if (clipOk && fileOk) {
        msg = fmt::format("Report (+ path) copied to clipboard and saved to:\n"
                          "{}\n"
                          "{}\n\n"
                          "{}",
                          path.string(), EbPauseActionBridgeDetail::pathClipboardNote(true), text);
#ifndef GEODE_IS_WINDOWS
        msg += fmt::format("\n\n{}", EbPauseActionBridgeDetail::mobileTransferHint());
#endif
    } else if (fileOk && !clipOk) {
        msg = fmt::format("Clipboard failed; saved to:\n"
                          "{}\n"
                          "{}\n\n"
                          "{}",
                          path.string(), EbPauseActionBridgeDetail::pathClipboardNote(false), text);
#ifndef GEODE_IS_WINDOWS
        msg += fmt::format("\n\n{}", EbPauseActionBridgeDetail::mobileTransferHint());
#endif
    } else if (clipOk && !fileOk) {
        msg = fmt::format("Report copied to clipboard.\nFile save failed: {}\n\n{}", fileErr, text);
    } else {
        msg = fmt::format("Copy failed (clipboard and file).\n"
                          "File: {}\n\n"
                          "{}",
                          fileErr, text);
    }
    EbDesktopAlert::info("EarlyBuffer Dev", msg.c_str());
}
