#ifndef IN_LEVEL_SETTINGS_FILTER_KEEPER_HPP
#define IN_LEVEL_SETTINGS_FILTER_KEEPER_HPP

#include <Geode/Geode.hpp>

class EbInLevelSettingsFilterKeeper {
public:
    static void attach(cocos2d::CCNode* popup);

    // True if our keeper is on an open settings popup in the scene.
    static bool isOpen();

    // Refresh bake status / button enable on the open popup (no-op if closed).
    static void refreshOpen();

    // After ConfigBundle import: pull store → open SettingNodeV3 drafts (clears green
    // uncommitted), then rebuild the popup next frame so keybind rows / Apply match.
    // Restores the active category after rebuild. No-op if settings are closed.
    static void reloadAfterImport();

    // Scene watchdog: attach tabs whenever EarlyBuffer settings open (Geode menu or EB).
    static void startWatchdog();

private:
    EbInLevelSettingsFilterKeeper() = delete;
};

#endif
