#ifndef SETTINGS_FILTER_WALK_HPP
#define SETTINGS_FILTER_WALK_HPP

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <vector>

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

#endif
