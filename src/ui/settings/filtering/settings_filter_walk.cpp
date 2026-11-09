#include "settings_filter_walk.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <vector>

auto EbSettingsFilterWalk::isOurCategoryChrome(geode::prelude::CCNode* node) -> bool {
    if (!node)
        return false;
    auto const id = node->getID();
    return id == "earlybuffer-settings-category-bar"_spr ||
           id == "earlybuffer-settings-category-menu"_spr;
}

auto EbSettingsFilterWalk::isCategoryBarScroll(geode::prelude::ScrollLayer* scroll) -> bool {
    if (!scroll)
        return false;
    if (scroll->getID() == "earlybuffer-settings-category-scroll"_spr)
        return true;
    for (auto* count = scroll->getParent(); count; count = count->getParent()) {
        if (count->getID() == "earlybuffer-settings-category-bar"_spr)
            return true;
    }
    return false;
}

auto EbSettingsFilterWalk::scrollLooksLikeSettingsList(geode::prelude::ScrollLayer* scroll) -> bool {
    if (!scroll || !scroll->m_contentLayer)
        return false;
    if (isCategoryBarScroll(scroll))
        return false;
    // Real ModSettingsPopup list is tall; chip strip is ~34px.
    if (!(scroll->getContentSize().height < 80.f)) {
        return true;
    }
    return false;
}

auto EbSettingsFilterWalk::collect(geode::prelude::CCNode* node,
                                 std::vector<geode::prelude::SettingNodeV3*>& settings,
                                 geode::prelude::ScrollLayer*& list) -> void {
    if (!node)
        return;
    // Do not descend into our chip bar — its nested ScrollLayer would steal `list`.
    if (isOurCategoryChrome(node))
        return;

    if (auto* scroll = geode::prelude::typeinfo_cast<geode::prelude::ScrollLayer*>(node)) {
        if (scrollLooksLikeSettingsList(scroll)) {
            if (!list || scroll->getContentSize().height > list->getContentSize().height) {
                list = scroll;
            }
        }
    }
    if (auto* sett = geode::prelude::typeinfo_cast<geode::prelude::SettingNodeV3*>(node)) {
        settings.push_back(sett);
        return;
    }
    auto* children = node->getChildren();
    if (!children)
        return;
    std::vector<geode::prelude::CCNode*> kids;
    kids.reserve(static_cast<size_t>(children->count()));
    for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(children))
        kids.push_back(child);
    for (auto* child : kids)
        collect(child, settings, list);
}
