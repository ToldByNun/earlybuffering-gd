#ifndef SETTINGS_CATEGORY_BAR_HPP
#define SETTINGS_CATEGORY_BAR_HPP

#include "../in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>

class EbSettingsFilterKeeperNode;

class EbSettingsCategoryBar : public geode::prelude::CCNode {
public:
    static constexpr float kBarH = 32.f;
    static constexpr float kSidePad = 2.f;
    static constexpr float kSegGap = 0.f;
    static constexpr float kMinFitScale = 0.20f;
    static constexpr float kMaxFitScale = 0.40f;

    bool initBar(EbSettingsFilterKeeperNode* owner, float width);

    void setCategory(SettingsCategory category);

    void setMenuTouchPriority(int priority);

    void onChip(geode::prelude::CCObject* sender);

    static EbSettingsCategoryBar* create(EbSettingsFilterKeeperNode* owner, float width);

private:
    void paintLabels(SettingsCategory active);

    void rebuild(SettingsCategory active);

    EbSettingsFilterKeeperNode* m_owner = nullptr;
    SettingsCategory m_active = SettingsCategory::Main;
    geode::prelude::CCNode* m_labels = nullptr;
    geode::prelude::CCMenu* m_menu = nullptr;
};

#endif
