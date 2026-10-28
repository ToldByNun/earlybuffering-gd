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

    // Geode ModSettingsPopup (search / titles / pages) for this mod.
    static void openModSettings();
    // Pause hub popup (Bake | Assist | Settings). No-op if already open.
    static void openHub();

private:
    PauseActionBridge() = delete;

    static void handleImportedPath(std::filesystem::path const& path);
};

#endif // EARLYBUFFER_UI_PAUSEACTIONBRIDGE_HPP
