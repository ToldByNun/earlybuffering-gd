#ifndef SETTINGS_TABS_HOOK_HPP
#define SETTINGS_TABS_HOOK_HPP

class SettingsPopupAttach;

#include "in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

class SettingsPopupAttach {
public:
    static bool nodeTreeHasOurSetting(geode::prelude::CCNode* root);

    static void tryAttachTabs(FLAlertLayer* layer);

private:
    SettingsPopupAttach() = delete;
};

class $modify(EbSettingsFLAlertHook, FLAlertLayer) {
public:
    void show();

private:
};

#endif
