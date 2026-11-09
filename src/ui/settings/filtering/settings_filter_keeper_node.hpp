#ifndef SETTINGS_FILTER_KEEPER_NODE_HPP
#define SETTINGS_FILTER_KEEPER_NODE_HPP

#include "../in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <vector>

class EbSettingsCategoryBar;

class EbSettingsFilterKeeperNode : public geode::prelude::CCNode {
public:
    SettingsCategory m_category = SettingsCategory::Main;
    EbSettingsCategoryBar* m_bar = nullptr;
    geode::prelude::ScrollLayer* m_list = nullptr;
    std::vector<geode::prelude::Ref<geode::prelude::SettingNodeV3>> m_all;
    size_t m_lastChildCount = 0;
    SettingsCategory m_appliedCat = SettingsCategory::Count;
    bool m_loggedShortList = false;
    float m_listFullH = 0.f;

    void setCategory(SettingsCategory category);

    void cycle(int delta);

    void layoutBar(geode::prelude::ScrollLayer* list, EbSettingsCategoryBar* bar);

    void ensureBar(geode::prelude::ScrollLayer* list);

    void absorbFromTree(geode::prelude::CCNode* popup);

    static bool nodeKeep(geode::prelude::SettingNodeV3* sett, SettingsCategory category);

    void refilter(bool force);

    bool initKeeper();

    void onTick(float);

    static EbSettingsFilterKeeperNode* create();

    static EbSettingsFilterKeeperNode* findOpen();

private:
    geode::prelude::ListenerHandle m_changeHandle{};
};

#endif
