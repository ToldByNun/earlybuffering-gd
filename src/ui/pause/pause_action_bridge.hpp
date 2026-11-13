#ifndef PAUSE_ACTION_BRIDGE_HPP
#define PAUSE_ACTION_BRIDGE_HPP

class EbPauseActionBridgeDetail;

#include "../desktop_menu/desktop_menu_alert.hpp"
#include <filesystem>
#include <string>

class PauseLayer;

// Pause / hub actions in one place. Hooks and popups call these; no UI layout here.
class EbPauseActionBridge {
public:
    static void refreshPauseLabels(PauseLayer* pause);

    static void importReplay();
    static void startBake(bool brute);
    static void cancelBake();
    static void cycleAssistMode();

    static void toggleDebug();
    static void resetLearning();
    static void toggleAutoTune();
    static void toggleTrajectory();
    static void exportConfig();
    static void importConfig();
    // Opens Cloud Configs browser (catalog / install / share).
    static void openCloudConfigs();

    // After ConfigBundle apply: sync overlay/visualizers, rebuild open settings UI,
    // then show successMsg on the next main-thread tick (keeps alert on top).
    static void afterConfigApplied(std::string const& successMsg);

    // Dev Lab actions (log-only stubs until session/eval/trace land).
    static void devStartRecord();
    static void devStopRecord();
    static void devEvalLast();
    static void devEvalSession();
    static void devExportSession();
    static void devClearTraces();
    static void devCopyReport();
    // Writes settings/clock/replay/TW/bake-probe logs under saveDir/diag/.
    static void devDumpDiag();
    // One-click research folder under saveDir/research/research_<stamp>/.
    static void devExportResearchPack();
    static void devCompareSessions();

    // Desktop ImGui shell / Geode popup: Main/Assist/Learn/Bake/Modes/Visual/UI/Keys/Dev.
    static void openModSettings();
    // Same as openModSettings (EB pause button). No-op if already open.
    static void openHub();
    // Bake poll: refresh Bake-page status on an open settings popup.
    static void refreshOpenSettings();

private:
    friend class EbPauseConfigActions;

    friend class EbPauseDevActions;

    EbPauseActionBridge() = delete;

    static void handleImportedPath(std::filesystem::path const& path);
    static void stubDevAction(char const* name);
};

#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/bake/bake_map.hpp"
#include "../../core/bake/bake_scheduler.hpp"
#include "../../core/bundle/config_bundle.hpp"
#include "../../core/platform/save_drop_paths.hpp"
#include "../../core/replay/replay_import.hpp"
#include "../../debug/assist_log.hpp"
#include "../../dev/capture/dev_trace_store.hpp"
#include "../../dev/dev_config.hpp"
#include "../../dev/dev_session.hpp"
#include "../../dev/reporting/dev_diag_dump.hpp"
#include "../../dev/reporting/dev_report.hpp"
#include "../cloud/cloud_config_popup.hpp"
#include "../overlays/dev_overlay.hpp"
#include "../overlays/status_overlay.hpp"
#include "../settings/in_level_settings_filter.hpp"
#include "../visualizers/bake_path_visualizer.hpp"
#include "../visualizers/trajectory_visualizer.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <optional>
#include <system_error>

#ifdef GEODE_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <commdlg.h>
#include <windows.h>
#endif

class EbPauseActionBridgeDetail {
public:
    static geode::prelude::CCLabelBMFont* findStatusLabel(PauseLayer* self);

    static geode::prelude::CCLabelBMFont* findModeLabel(PauseLayer* self);

#ifdef GEODE_IS_WINDOWS
    static std::optional<std::filesystem::path> pickReplayFileWin32() {
        wchar_t fileBuf[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"Replays (GDR/MHR/SLC)\0*.gdr;*.gdr2;*.mhr;*.json;*.slc\0"
                          L"All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (!(!GetOpenFileNameW(&ofn))) {
            return std::filesystem::path(fileBuf);
        }
        return std::nullopt;
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
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                    (saveAs ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (saveAs) {
            if (!GetSaveFileNameW(&ofn))
                return std::nullopt;
        } else {
            if (!GetOpenFileNameW(&ofn))
                return std::nullopt;
        }
        return std::filesystem::path(fileBuf);
    }
#endif
    static bool tryCopyPathToClipboard(std::filesystem::path const& path);

    static std::string pathClipboardNote(bool clipOk);

#ifndef GEODE_IS_WINDOWS
    // Neutral sandbox wording (Files / share / file manager / Finder-via-device).
    static char const* mobileTransferHint() {
        return "Use Files, Share, or a file manager "
               "(on Apple devices: Files app or Finder via the device).";
    }
#endif

    static std::string formatExportSuccess(std::filesystem::path const& path);

    static std::string formatImportSuccess(std::optional<std::filesystem::path> const& path);

#ifndef GEODE_IS_WINDOWS
    // Non-Win: pick newest replay from SaveDropPaths::replayDropDir().
    // nullopt = user was already alerted (ensureDirs fail / empty folder).
    static std::optional<std::filesystem::path> pickReplayFromDropDir() {
        auto& drops = EbSaveDropPaths::get();
        std::string err;
        if (!drops.ensureDirs(err)) {
            EbDesktopAlert::info(
                "EarlyBuffer",
                err.empty() ? "Could not create the replay drop folder." : err.c_str());
            return std::nullopt;
        }

        auto const directory = drops.replayDropDir();
        auto const candidates = drops.listReplayCandidates();
        if (candidates.empty()) {
            bool const clipOk = tryCopyPathToClipboard(directory);
            auto const message =
                fmt::format("No replay in the drop folder.\n\n"
                            "{}\n"
                            "{}\n\n"
                            "{}\n\n"
                            "Put one file there (.gdr .gdr2 .mhr .json .slc).\n"
                            "Empty files are ignored.\n"
                            "If several files are present, Import uses the newest.",
                            directory.string(), pathClipboardNote(clipOk), mobileTransferHint());
            EbDesktopAlert::info("EarlyBuffer", message);
            return std::nullopt;
        }

        auto const& chosen = candidates.front(); // newest mtime
        if (candidates.size() > 1) {
            EbAssistLog::info(fmt::format("Import: {} file(s) in drop folder — using newest '{}'",
                                        candidates.size(), chosen.filename().string()));
        }
        return chosen;
    }
#endif

private:
};

#endif
