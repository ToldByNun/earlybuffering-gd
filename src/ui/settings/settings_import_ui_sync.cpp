#include "settings_import_ui_sync.hpp"
#include "filtering/settings_filter_keeper_node.hpp"
#include "filtering/settings_filter_walk.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <string>
#include <vector>

auto EbSettingsImportUiSync::syncNodesFromStore(geode::prelude::CCNode* root,
                                              std::string const& ourId) -> void {
    if (!root)
        return;
    std::vector<geode::prelude::SettingNodeV3*> settings;
    geode::prelude::ScrollLayer* list = nullptr;
    EbSettingsFilterWalk::collect(root, settings, list);
    for (auto* sett : settings) {
        if (!sett)
            continue;
        auto setting = sett->getSetting();
        if (!setting || setting->getModID() != ourId)
            continue;
        if (syncValueNode<geode::prelude::BoolSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::IntSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::FloatSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::StringSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::Color3BSettingV3>(sett))
            continue;
        if (syncValueNode<geode::prelude::Color4BSettingV3>(sett))
            continue;
        // KeybindSettingNodeV3 has no public setValue — popup rebuild covers it.
    }
}

auto EbSettingsImportUiSync::findOurSettingsPopup(geode::prelude::CCNode* scene,
                                                std::string const& ourId)
    -> geode::prelude::CCNode* {
    if (!scene)
        return nullptr;
    if (auto* keeper = EbSettingsFilterKeeperNode::findOpen()) {
        if (auto* parent = keeper->getParent())
            return parent;
    }
    for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(scene->getChildren())) {
        auto* alert = geode::prelude::typeinfo_cast<FLAlertLayer*>(child);
        if (!alert)
            continue;
        std::vector<geode::prelude::SettingNodeV3*> settings;
        geode::prelude::ScrollLayer* list = nullptr;
        EbSettingsFilterWalk::collect(alert, settings, list);
        for (auto* sett : settings) {
            if (!sett)
                continue;
            auto state = sett->getSetting();
            if (state && state->getModID() == ourId)
                return alert;
        }
    }
    return nullptr;
}
