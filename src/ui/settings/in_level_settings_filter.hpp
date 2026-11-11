#ifndef IN_LEVEL_SETTINGS_FILTER_HPP
#define IN_LEVEL_SETTINGS_FILTER_HPP

class EbSettingsCategoryMap;
class EbSettingsFilterWalk;
class EbSettingsCategoryBar;
class EbSettingsFilterKeeperNode;
class EbSettingsImportUiSync;
class EbSettingsTabWatchdog;
class EbSettingsTabWatchdogState;

#include <Geode/Geode.hpp>
#include <string>

// Categories for the Geode ModSettingsPopup chip / segment bar + desktop shell.
// Smart folded into Main; Scope folded into Assist; UI chrome on its own tab.
enum class SettingsCategory : int {
    Main = 0,
    Assist,
    Learn,
    Bake,
    Modes,
    Visual,
    UI,
    Keys,
    Dev,
    Count
};

// Filters Geode settings rows by SettingsCategory.
// Non-matching rows are reparented out of the list (Geode search style); scroll Y preserved.
class EbInLevelSettingsFilter {
public:
    static char const* categoryName(SettingsCategory category);
    // Exhaustive map. Unknown keys return SettingsCategory::Count (never Assist dump).
    static SettingsCategory categoryForKey(std::string const& key);
    static bool keepKey(std::string const& key, SettingsCategory category);

    // Legacy subset: Main∪Scope∪Assist∪Visual∪Bake (drops Modes/Keys/Dev).
    static bool keepKey(std::string const& key);

    static void apply(cocos2d::CCNode* popup, SettingsCategory category);

    static float preservedContentY(float posBefore, float heightBefore, float heightAfter);

    static bool selfCheck(std::string& report);

private:
    EbInLevelSettingsFilter() = delete;
};

// Attaches to every EarlyBuffer settings popup: category bar + live re-filter + Bake/Assist
// actions.

#include "filtering/in_level_settings_filter_keeper.hpp"

#endif
