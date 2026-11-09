#ifndef SETTINGS_CATEGORY_MAP_HPP
#define SETTINGS_CATEGORY_MAP_HPP

#include "../../../core/bundle/config_bundle_keys.hpp"
#include "../in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <string>

class EbSettingsCategoryMap {
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

    EbSettingsCategoryMap() = delete;
};

#endif
