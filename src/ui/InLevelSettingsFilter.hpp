#ifndef EARLYBUFFER_UI_INLEVELSETTINGSFILTER_HPP
#define EARLYBUFFER_UI_INLEVELSETTINGSFILTER_HPP

#include <Geode/Geode.hpp>
#include <string>

// Categories for the Geode ModSettingsPopup page switcher (◀ NAME ▶).
enum class SettingsCategory : int {
    Main = 0,
    Bake,
    Ingame,
    Assist,
    Visual,
    Modes,
    Keys,
    Dev,
    Count
};

// Filters Geode settings rows by SettingsCategory.
// Non-matching rows are reparented out of the list (Geode search style); scroll Y preserved.
class InLevelSettingsFilter {
public:
    static char const* categoryName(SettingsCategory cat);
    static SettingsCategory categoryForKey(std::string const& key);
    static bool keepKey(std::string const& key, SettingsCategory cat);

    // Legacy: Main∪Ingame∪Assist∪Visual.
    static bool keepKey(std::string const& key);

    static void apply(cocos2d::CCNode* popup, SettingsCategory cat);

    static float preservedContentY(float posBefore, float heightBefore, float heightAfter);

    static bool selfCheck(std::string& report);

private:
    InLevelSettingsFilter() = delete;
};

// Attaches to every EarlyBuffer settings popup: category bar + live re-filter + Bake/Assist actions.
class InLevelSettingsFilterKeeper {
public:
    static void attach(cocos2d::CCNode* popup);

    // True if our keeper is on an open settings popup in the scene.
    static bool isOpen();

    // Refresh bake status / button enable on the open popup (no-op if closed).
    static void refreshOpen();

    // Scene watchdog: attach tabs whenever EarlyBuffer settings open (Geode menu or EB).
    static void startWatchdog();

private:
    InLevelSettingsFilterKeeper() = delete;
};

#endif // EARLYBUFFER_UI_INLEVELSETTINGSFILTER_HPP
