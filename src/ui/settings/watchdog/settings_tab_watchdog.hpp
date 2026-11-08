#ifndef SETTINGS_TAB_WATCHDOG_HPP
#define SETTINGS_TAB_WATCHDOG_HPP

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <vector>

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
