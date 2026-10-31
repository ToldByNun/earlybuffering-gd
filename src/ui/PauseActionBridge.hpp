#ifndef EARLYBUFFER_UI_PAUSEACTIONBRIDGE_HPP
#define EARLYBUFFER_UI_PAUSEACTIONBRIDGE_HPP

#include <filesystem>

class PauseLayer;

// Pause / hub actions in one place. Hooks and popups call these; no UI layout here.
class PauseActionBridge {
public:
    static void refreshPauseLabels(PauseLayer* pause);

    static void importReplay();
    static void startBake(bool brute);
    static void cancelBake();
    static void cycleAssistMode();

    static void cyclePreset();
    static void toggleDebug();
    static void resetLearning();
    static void toggleAutoTune();
    static void toggleTrajectory();
    static void exportConfig();
    static void importConfig();

    // Dev Lab actions (Commit 1 stubs — log only until session/eval/trace land).
    static void devStartRecord();
    static void devStopRecord();
    static void devEvalLast();
    static void devEvalSession();
    static void devExportSession();
    static void devClearTraces();
    static void devCopyReport();
    // Writes settings/clock/replay/TW/bake-probe logs under saveDir/diag/.
    static void devDumpDiag();

    // Geode ModSettingsPopup with Main/Scope/Assist/Bake/Modes/Visual/Keys/Dev switcher.
    static void openModSettings();
    // Same as openModSettings (EB pause button). No-op if already open.
    static void openHub();
    // Bake poll: refresh Bake-page status on an open settings popup.
    static void refreshOpenSettings();

private:
    PauseActionBridge() = delete;

    static void handleImportedPath(std::filesystem::path const& path);
    static void stubDevAction(char const* name);
};

#endif // EARLYBUFFER_UI_PAUSEACTIONBRIDGE_HPP
