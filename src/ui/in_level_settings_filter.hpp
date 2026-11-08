#ifndef IN_LEVEL_SETTINGS_FILTER_HPP
#define IN_LEVEL_SETTINGS_FILTER_HPP

class SettingsCategoryMap;
class SettingsFilterWalk;
class SettingsCategoryBar;
class SettingsFilterKeeperNode;
class SettingsImportUiSync;
class SettingsTabWatchdog;
class SettingsTabWatchdogState;

#include <Geode/Geode.hpp>
#include <string>

// Categories for the Geode ModSettingsPopup chip / segment bar.
// Ingame folded into Main; Scope is its own page. Count stays 8.
enum class SettingsCategory : int {
    Main = 0,
    Scope,
    Assist,
    Bake,
    Modes,
    Visual,
    Keys,
    Dev,
    Count
};

// Filters Geode settings rows by SettingsCategory.
// Non-matching rows are reparented out of the list (Geode search style); scroll Y preserved.
class InLevelSettingsFilter {
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
    InLevelSettingsFilter() = delete;
};

// Attaches to every EarlyBuffer settings popup: category bar + live re-filter + Bake/Assist
// actions.
class InLevelSettingsFilterKeeper {
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
    InLevelSettingsFilterKeeper() = delete;
};

#include "../core/config_bundle_keys.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cmath>
#include <cocos-ext.h>
#include <cstring>
#include <fmt/format.h>
#include <vector>

class SettingsCategoryMap {
public:
    class Entry {
    public:
        char const* key;
        SettingsCategory cat;

    private:
    };

    static constexpr int kCount = 167;

    static Entry const* begin();
    static Entry const* end();

    static SettingsCategory lookup(std::string const& key);

private:
    static Entry const kEntries[kCount];

    SettingsCategoryMap() = delete;
};

class SettingsFilterWalk {
public:
    static bool isOurCategoryChrome(geode::prelude::CCNode* node);

    // Legacy: older builds nested a ScrollLayer in the chip bar; still ignore those.
    static bool isCategoryBarScroll(geode::prelude::ScrollLayer* scroll);

    static bool scrollLooksLikeSettingsList(geode::prelude::ScrollLayer* scroll);

    static void collect(geode::prelude::CCNode* node,
                        std::vector<geode::prelude::SettingNodeV3*>& settings,
                        geode::prelude::ScrollLayer*& list);

private:
    SettingsFilterWalk() = delete;
};

class SettingsCategoryBar : public geode::prelude::CCNode {
public:
    static constexpr float kBarH = 32.f;
    static constexpr float kSidePad = 2.f;
    static constexpr float kSegGap = 0.f;
    static constexpr float kMinFitScale = 0.20f;
    static constexpr float kMaxFitScale = 0.40f;

    bool initBar(SettingsFilterKeeperNode* owner, float width);

    void setCategory(SettingsCategory category);

    void setMenuTouchPriority(int priority);

    void onChip(geode::prelude::CCObject* sender);

    static SettingsCategoryBar* create(SettingsFilterKeeperNode* owner, float width);

private:
    void paintLabels(SettingsCategory active);

    void rebuild(SettingsCategory active);

    SettingsFilterKeeperNode* m_owner = nullptr;
    SettingsCategory m_active = SettingsCategory::Main;
    geode::prelude::CCNode* m_labels = nullptr;
    geode::prelude::CCMenu* m_menu = nullptr;
};

class SettingsFilterKeeperNode : public geode::prelude::CCNode {
public:
    SettingsCategory m_category = SettingsCategory::Main;
    SettingsCategoryBar* m_bar = nullptr;
    geode::prelude::ScrollLayer* m_list = nullptr;
    std::vector<geode::prelude::Ref<geode::prelude::SettingNodeV3>> m_all;
    size_t m_lastChildCount = 0;
    SettingsCategory m_appliedCat = SettingsCategory::Count;
    bool m_loggedShortList = false;
    float m_listFullH = 0.f;

    void setCategory(SettingsCategory category);

    void cycle(int delta);

    void layoutBar(geode::prelude::ScrollLayer* list, SettingsCategoryBar* bar);

    void ensureBar(geode::prelude::ScrollLayer* list);

    void absorbFromTree(geode::prelude::CCNode* popup);

    static bool nodeKeep(geode::prelude::SettingNodeV3* sett, SettingsCategory category);

    void refilter(bool force);

    bool initKeeper();

    void onTick(float);

    static SettingsFilterKeeperNode* create();

    static SettingsFilterKeeperNode* findOpen();

private:
    geode::prelude::ListenerHandle m_changeHandle{};
};

class SettingsImportUiSync {
public:
    template <class SettingT> static bool syncValueNode(geode::prelude::SettingNodeV3* sett) {
        auto* node =
            geode::prelude::typeinfo_cast<geode::prelude::SettingValueNodeV3<SettingT>*>(sett);
        if (!node)
            return false;
        node->setValue(node->getSetting()->getValue(), nullptr);
        return true;
    }

    static void syncNodesFromStore(geode::prelude::CCNode* root, std::string const& ourId);

    static geode::prelude::CCNode* findOurSettingsPopup(geode::prelude::CCNode* scene,
                                                        std::string const& ourId);

private:
    SettingsImportUiSync() = delete;
};

class SettingsTabWatchdog : public geode::prelude::CCNode {
public:
    static bool popupIsOurSettings(geode::prelude::CCNode* root);

    static void collectAlerts(geode::prelude::CCNode* node, std::vector<FLAlertLayer*>& out);

    static geode::prelude::CCNode* findUnattachedOurPopup();

    void ensureOnScene();

    void onTick(float);

    bool initWatchdog();

    static SettingsTabWatchdog* create();

private:
};

class SettingsTabWatchdogState {
public:
    static geode::prelude::Ref<SettingsTabWatchdog>& slot();

private:
    SettingsTabWatchdogState() = delete;
};

#endif
