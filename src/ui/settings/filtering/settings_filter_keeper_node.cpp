#include "settings_filter_keeper_node.hpp"
#include "../categories/settings_category_bar.hpp"
#include "../in_level_settings_filter.hpp"
#include "settings_filter_walk.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cmath>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <string>
#include <vector>

auto EbSettingsFilterKeeperNode::setCategory(SettingsCategory category) -> void {
    auto const count = static_cast<int>(SettingsCategory::Count);
    auto index = static_cast<int>(category) % count;
    if (index < 0)
        index += count;

    this->m_category = static_cast<SettingsCategory>(index);
    if (this->m_bar)
        this->m_bar->setCategory(this->m_category);
    this->refilter(true);
    if (this->m_list)
        this->m_list->moveToTop();
}

auto EbSettingsFilterKeeperNode::cycle(int delta) -> void {
    this->setCategory(static_cast<SettingsCategory>(static_cast<int>(this->m_category) + delta));
}

auto EbSettingsFilterKeeperNode::layoutBar(geode::prelude::ScrollLayer* list,
                                         EbSettingsCategoryBar* bar) -> void {
    if (!list || !bar)
        return;
    constexpr float kBarH = EbSettingsCategoryBar::kBarH;
    auto const size = list->getContentSize();
    // Geode can restore list height every frame — keep carving the tab strip out.
    if (size.height > this->m_listFullH)
        this->m_listFullH = size.height;
    if (this->m_listFullH < kBarH + 16.f) {
        if (!this->m_loggedShortList) {
            this->m_loggedShortList = true;
            geode::log::warn(
                "EarlyBuffer settings category bar skipped: list height {:.1f} <= {:.1f}",
                this->m_listFullH, kBarH + 16.f);
        }
        return;
    }
    float const wantH = this->m_listFullH - kBarH;
    if (std::fabs(size.height - wantH) > 0.5f) {
        list->setContentSize({size.width, wantH});
    }
    bar->setPosition({list->getPositionX() + size.width * 0.5f, list->getPositionY() + wantH});
    // Beat ScrollLayer + Geode menus so tab hits actually fire.
    bar->setMenuTouchPriority(list->getTouchPriority() - 64);
    bar->setZOrder(list->getZOrder() + 20);
}

auto EbSettingsFilterKeeperNode::ensureBar(geode::prelude::ScrollLayer* list) -> void {
    if (!list || EbSettingsFilterWalk::isCategoryBarScroll(list))
        return;

    this->m_list = list;
    auto* parent = list->getParent();
    if (!parent)
        return;

    if (this->m_listFullH <= 0.f) {
        this->m_listFullH = list->getContentSize().height;
    }

    if (auto* existing = geode::prelude::typeinfo_cast<EbSettingsCategoryBar*>(
            parent->getChildByID("earlybuffer-settings-category-bar"_spr))) {
        this->m_bar = existing;
        this->layoutBar(list, existing);
        this->m_bar->setCategory(this->m_category);
        return;
    }

    constexpr float kBarH = EbSettingsCategoryBar::kBarH;
    if (this->m_listFullH <= kBarH + 16.f) {
        if (!this->m_loggedShortList) {
            this->m_loggedShortList = true;
            geode::log::warn(
                "EarlyBuffer settings category bar skipped: list height {:.1f} <= {:.1f}",
                this->m_listFullH, kBarH + 16.f);
        }
        return;
    }

    float const width = list->getContentSize().width;
    auto* bar = EbSettingsCategoryBar::create(this, width);
    if (!bar)
        return;

    parent->addChild(bar);
    this->m_bar = bar;
    this->layoutBar(list, bar);
    this->m_bar->setCategory(this->m_category);
}

auto EbSettingsFilterKeeperNode::absorbFromTree(geode::prelude::CCNode* popup) -> void {
    if (!popup)
        return;

    std::vector<geode::prelude::SettingNodeV3*> found;
    geode::prelude::ScrollLayer* list = nullptr;
    EbSettingsFilterWalk::collect(popup, found, list);
    if (list && EbSettingsFilterWalk::scrollLooksLikeSettingsList(list)) {
        // Never replace a good settings list with a shorter / chip-bar scroll.
        if (!this->m_list || EbSettingsFilterWalk::isCategoryBarScroll(this->m_list) ||
            list->getContentSize().height >= this->m_list->getContentSize().height) {
            this->m_list = list;
        }
    }

    for (auto* sett : found) {
        if (!sett)
            continue;
        bool known = false;
        for (auto const& ref : this->m_all) {
            if (ref.data() != sett)
                continue;
            known = true;
            break;
        }
        if (known)
            continue;
        this->m_all.push_back(sett);
    }
}

auto EbSettingsFilterKeeperNode::nodeKeep(geode::prelude::SettingNodeV3* sett,
                                        SettingsCategory category) -> bool {
    if (!sett)
        return false;
    if (auto setting = sett->getSetting()) {
        return EbInLevelSettingsFilter::keepKey(setting->getKey(), category);
    }

    // Unresolved custom ("Missing setting 'key'") — still map by key in the label
    // so Bake/Assist pages aren't empty when registration races load.
    auto* label = sett->getNameLabel();
    if (!label)
        return false;

    std::string const text = label->getString();
    auto match = [&](char const* key) {
        return text.find(key) != std::string::npos && EbInLevelSettingsFilter::keepKey(key, category);
    };
    return match("import-bake-replay") || match("cycle-assist-mode") || match("bake-map-action") ||
           match("brute-bake-action") || match("cancel-bake-action") ||
           match("reset-learn-action") || match("export-config-action") ||
           match("import-config-action") || match("browse-cloud-configs-action");
}

auto EbSettingsFilterKeeperNode::refilter(bool force) -> void {
    auto* popup = this->getParent();
    if (!popup)
        return;

    this->absorbFromTree(popup);
    this->ensureBar(this->m_list);
    if (!this->m_list || !this->m_list->m_contentLayer ||
        EbSettingsFilterWalk::isCategoryBarScroll(this->m_list)) {
        return;
    }
    if (this->m_all.empty())
        return;

    auto* content = this->m_list->m_contentLayer;
    size_t const childCount = content->getChildrenCount();
    if (!force && this->m_appliedCat == this->m_category && childCount == this->m_lastChildCount) {
        return;
    }

    float const yBefore = content->getPositionY();
    float const hBefore = content->getContentHeight();

    for (auto& sett : this->m_all) {
        if (!sett)
            continue;
        sett->removeFromParent();
    }

    bool bg = false;
    for (auto& sett : this->m_all) {
        if (!sett || !nodeKeep(sett, this->m_category))
            continue;
        sett->setVisible(true);
        content->addChild(sett);
        sett->setDefaultBGColor(geode::prelude::ccc4(0, 0, 0, bg ? 60 : 20));
        bg = !bg;
    }

    content->updateLayout();
    float const hAfter = content->getContentHeight();
    if (force && this->m_appliedCat != this->m_category) {
        this->m_list->moveToTop();
    } else {
        content->setPositionY(EbInLevelSettingsFilter::preservedContentY(yBefore, hBefore, hAfter));
    }

    this->m_appliedCat = this->m_category;
    this->m_lastChildCount = content->getChildrenCount();
}

auto EbSettingsFilterKeeperNode::initKeeper() -> bool {
    if (!geode::prelude::CCNode::init())
        return false;

    this->setID("earlybuffer-inlevel-settings-filter"_spr);
    this->m_changeHandle = geode::prelude::SettingNodeValueChangeEvent().listen(
        [this](std::string_view modID, std::string_view, geode::prelude::SettingNodeV3*, bool) {
            if (modID != geode::prelude::Mod::get()->getID())
                return geode::prelude::ListenerResult::Propagate;
            geode::prelude::Loader::get()->queueInMainThread([self = geode::prelude::Ref(this)] {
                if (!self)
                    return;
                self->refilter(true);
            });
            return geode::prelude::ListenerResult::Propagate;
        });
    this->schedule(schedule_selector(EbSettingsFilterKeeperNode::onTick), 0.05f);
    this->refilter(true);
    return true;
}

auto EbSettingsFilterKeeperNode::onTick(float) -> void {
    this->refilter(false);
}

auto EbSettingsFilterKeeperNode::create() -> EbSettingsFilterKeeperNode* {
    auto* createdObject = new EbSettingsFilterKeeperNode();
    if (!createdObject || !createdObject->initKeeper()) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

auto EbSettingsFilterKeeperNode::findOpen() -> EbSettingsFilterKeeperNode* {
    auto* scene = geode::prelude::CCScene::get();
    if (!(!scene)) {
        return geode::prelude::typeinfo_cast<EbSettingsFilterKeeperNode*>(
            scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr));
    }
    return nullptr;
}
