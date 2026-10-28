#ifndef EARLYBUFFER_UI_INLEVELSETTINGSFILTER_HPP
#define EARLYBUFFER_UI_INLEVELSETTINGSFILTER_HPP

#include <Geode/Geode.hpp>
#include <string>

// Categories for the Geode ModSettingsPopup page switcher (◀ NAME ▶).
enum class SettingsCategory : int {
    Main = 0,
    Ingame,
    Assist,
    Visual,
    Modes,
    Keys,
    Count
};

// Filters Geode settings rows by SettingsCategory.
// Visibility-only + SimpleColumnLayout::ignoreInvisibleChildren; scroll Y preserved.
class InLevelSettingsFilter {
public:
    static char const* categoryName(SettingsCategory cat);
    static SettingsCategory categoryForKey(std::string const& key);
    static bool keepKey(std::string const& key, SettingsCategory cat);

    // Legacy smoke helper: Main∪Ingame∪Assist∪Visual pause-ish set.
    static bool keepKey(std::string const& key);

    static void apply(cocos2d::CCNode* popup, SettingsCategory cat);

    static float preservedContentY(float posBefore, float heightBefore, float heightAfter);

    static bool selfCheck(std::string& report);

private:
    InLevelSettingsFilter() = delete;
};

// Attaches to every EarlyBuffer settings popup: category bar + live re-filter.
class InLevelSettingsFilterKeeper {
public:
    static void attach(cocos2d::CCNode* popup);

private:
    InLevelSettingsFilterKeeper() = delete;
};

#endif // EARLYBUFFER_UI_INLEVELSETTINGSFILTER_HPP
