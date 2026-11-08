#include "in_level_settings_filter_keeper.hpp"
#include "../in_level_settings_filter.hpp"
#include "../settings_import_ui_sync.hpp"
#include "../watchdog/settings_tab_watchdog.hpp"
#include "settings_filter_keeper_node.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <string>

void InLevelSettingsFilterKeeper::attach(geode::prelude::CCNode* popup) {
    if (!popup)
        return;
    if (popup->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
        return;

    auto* keeper = SettingsFilterKeeperNode::create();
    if (!keeper)
        return;
    popup->addChild(keeper);
}

bool InLevelSettingsFilterKeeper::isOpen() {
    return SettingsFilterKeeperNode::findOpen() != nullptr;
}

void InLevelSettingsFilterKeeper::refreshOpen() {
    // Action button rows refresh themselves via onTick.
}

void InLevelSettingsFilterKeeper::reloadAfterImport() {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    std::string const ourId = mod->getID();

    SettingsCategory restoreCat = SettingsCategory::Main;
    if (auto* open = SettingsFilterKeeperNode::findOpen()) {
        restoreCat = open->m_category;
    }

    geode::prelude::CCNode* popup = SettingsImportUiSync::findOurSettingsPopup(scene, ourId);
    if (!popup)
        return;

    SettingsImportUiSync::syncNodesFromStore(popup, ourId);

    int const restoreI = static_cast<int>(restoreCat);
    // Rebuild next frame: import runs from a SettingNode button callback — do not
    // destroy the popup until that stack unwinds. Rebuild also refreshes keybinds.
    geode::prelude::Loader::get()->queueInMainThread([restoreI] {
        auto* scene = geode::prelude::CCScene::get();
        if (!scene)
            return;
        auto* mod = geode::prelude::Mod::get();
        if (!mod)
            return;

        if (auto* popup = SettingsImportUiSync::findOurSettingsPopup(scene, mod->getID())) {
            popup->removeFromParentAndCleanup(true);
        }
        auto* fresh = geode::openSettingsPopup(mod, false);
        if (!fresh)
            return;
        InLevelSettingsFilterKeeper::attach(fresh);
        if (auto* keeper = SettingsFilterKeeperNode::findOpen()) {
            auto const category = static_cast<SettingsCategory>(restoreI);
            if (category >= SettingsCategory::Main && category < SettingsCategory::Count) {
                keeper->setCategory(category);
            }
        }
    });
}

void InLevelSettingsFilterKeeper::startWatchdog() {
    auto& slot = SettingsTabWatchdogState::slot();
    if (!slot) {
        slot = SettingsTabWatchdog::create();
    }
    if (!slot)
        return;
    auto* scene = geode::prelude::CCScene::get();
    if (!scene) {
        geode::prelude::Loader::get()->queueInMainThread(
            [] { InLevelSettingsFilterKeeper::startWatchdog(); });
        return;
    }
    slot->ensureOnScene();
}
