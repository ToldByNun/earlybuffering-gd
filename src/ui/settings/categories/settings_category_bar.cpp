#include "settings_category_bar.hpp"
#include "../filtering/settings_filter_keeper_node.hpp"
#include "../in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>

void SettingsCategoryBar::onChip(geode::prelude::CCObject* sender) {
    auto* item = geode::prelude::typeinfo_cast<geode::prelude::CCMenuItem*>(sender);
    if (!item || !this->m_owner)
        return;
    int const tag = item->getTag();
    if (tag < 0 || tag >= static_cast<int>(SettingsCategory::Count))
        return;
    auto* owner = this->m_owner;
    auto const category = static_cast<SettingsCategory>(tag);
    // Defer past the menu callback so Geode touch dispatch stays stable.
    geode::prelude::Loader::get()->queueInMainThread(
        [owner = geode::prelude::Ref(owner), category] {
            if (!owner)
                return;
            owner->setCategory(category);
        });
}

auto SettingsCategoryBar::initBar(SettingsFilterKeeperNode* owner, float width) -> bool {
    if (!geode::prelude::CCNode::init())
        return false;
    if (!owner || width < 40.f)
        return false;

    this->m_owner = owner;
    this->m_active = SettingsCategory::Main;
    this->setID("earlybuffer-settings-category-bar"_spr);
    this->setContentSize({width, kBarH});
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.f});

    this->m_labels = geode::prelude::CCNode::create();
    if (!this->m_labels)
        return false;
    this->m_labels->setContentSize({width, kBarH});
    this->m_labels->setPosition({0.f, 0.f});
    this->addChild(this->m_labels, 1);

    this->m_menu = geode::prelude::CCMenu::create();
    if (!this->m_menu)
        return false;
    this->m_menu->setID("earlybuffer-settings-category-menu"_spr);
    this->m_menu->setContentSize({width, kBarH});
    this->m_menu->setPosition({0.f, 0.f});
    this->m_menu->ignoreAnchorPointForPosition(false);
    this->m_menu->setAnchorPoint({0.f, 0.f});
    this->addChild(this->m_menu, 2);

    this->rebuild(this->m_active);
    return true;
}

auto SettingsCategoryBar::setCategory(SettingsCategory category) -> void {
    if (category == this->m_active)
        return;
    this->m_active = category;
    // Never destroy menu items mid-click — only recolor labels.
    this->paintLabels(category);
}

auto SettingsCategoryBar::setMenuTouchPriority(int priority) -> void {
    if (this->m_menu)
        this->m_menu->setTouchPriority(priority);
}

auto SettingsCategoryBar::create(SettingsFilterKeeperNode* owner, float width)
    -> SettingsCategoryBar* {
    auto* createdObject = new SettingsCategoryBar();
    if (!createdObject || !createdObject->initBar(owner, width)) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

auto SettingsCategoryBar::paintLabels(SettingsCategory active) -> void {
    if (!this->m_labels)
        return;
    auto* kids = this->m_labels->getChildren();
    if (!kids)
        return;
    for (unsigned index = 0; index < kids->count(); ++index) {
        auto* label = geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(
            kids->objectAtIndex(index));
        if (!label)
            continue;
        bool const isActive = static_cast<int>(active) == static_cast<int>(index);
        if (isActive) {
            label->setColor({255, 255, 255});
            label->setOpacity(255);
        } else {
            label->setColor({150, 150, 150});
            label->setOpacity(200);
        }
    }
}

auto SettingsCategoryBar::rebuild(SettingsCategory active) -> void {
    if (!this->m_menu || !this->m_labels)
        return;
    this->m_menu->removeAllChildren();
    this->m_labels->removeAllChildren();

    float const barW = this->getContentSize().width;
    int const count = static_cast<int>(SettingsCategory::Count);
    if (count <= 0)
        return;

    float const segW = (barW - kSidePad * 2.f) / static_cast<float>(count);
    if (segW < 8.f)
        return;

    this->m_menu->setContentSize({barW, kBarH});
    float x = kSidePad;
    for (int index = 0; index < count; ++index) {
        auto const category = static_cast<SettingsCategory>(index);
        bool const isActive = category == active;

        auto* label = geode::prelude::CCLabelBMFont::create(
            InLevelSettingsFilter::categoryName(category), "bigFont.fnt");
        if (label) {
            float scale = kMaxFitScale;
            float const maxW = segW - 4.f;
            if (label->getContentSize().width > 0.f) {
                float const fit = maxW / label->getContentSize().width;
                if (fit < scale)
                    scale = fit;
            }
            if (scale < kMinFitScale)
                scale = kMinFitScale;
            label->setScale(scale);
            label->setAnchorPoint({0.5f, 0.5f});
            label->setPosition({x + segW * 0.5f, kBarH * 0.5f});
            if (isActive) {
                label->setColor({255, 255, 255});
                label->setOpacity(255);
            } else {
                label->setColor({150, 150, 150});
                label->setOpacity(200);
            }
            this->m_labels->addChild(label);
        }

        // Near-invisible hit plate (opacity 0 often skips hit-tests in cocos).
        auto* hit = geode::prelude::CCScale9Sprite::create("square02b_001.png");
        if (!hit) {
            x += segW;
            continue;
        }
        hit->setOpacity(1);
        hit->setContentSize({segW, kBarH});

        auto* item =
            CCMenuItemSpriteExtra::create(hit, this, menu_selector(SettingsCategoryBar::onChip));
        if (!item) {
            x += segW;
            continue;
        }
        item->setTag(static_cast<int>(category));
        item->setContentSize({segW, kBarH});
        if (auto* img = item->getNormalImage()) {
            img->setAnchorPoint({0.5f, 0.5f});
            img->setPosition({segW * 0.5f, kBarH * 0.5f});
        }
        item->setPosition({x + segW * 0.5f, kBarH * 0.5f});
        this->m_menu->addChild(item);
        x += segW;
    }
}
