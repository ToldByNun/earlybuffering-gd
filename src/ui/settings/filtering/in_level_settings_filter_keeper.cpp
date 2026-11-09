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

void EbInLevelSettingsFilterKeeper::attach(geode::prelude::CCNode* popup) {
    if (!popup)
        return;
    if (popup->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
        return;

    auto* keeper = EbSettingsFilterKeeperNode::create();
    if (!keeper)
        return;
    popup->addChild(keeper);
}

bool EbInLevelSettingsFilterKeeper::isOpen() {
    return EbSettingsFilterKeeperNode::findOpen() != nullptr;
}

void EbInLevelSettingsFilterKeeper::refreshOpen() {
    // Action button rows refresh themselves via onTick.
}

void EbInLevelSettingsFilterKeeper::reloadAfterImport() {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    std::string const ourId = mod->getID();

    SettingsCategory restoreCat = SettingsCategory::Main;
    if (auto* open = EbSettingsFilterKeeperNode::findOpen()) {
        restoreCat = open->m_category;
    }

    geode::prelude::CCNode* popup = EbSettingsImportUiSync::findOurSettingsPopup(scene, ourId);
    if (!popup)
        return;

    EbSettingsImportUiSync::syncNodesFromStore(popup, ourId);

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

        if (auto* popup = EbSettingsImportUiSync::findOurSettingsPopup(scene, mod->getID())) {
            popup->removeFromParentAndCleanup(true);
        }
        auto* fresh = geode::openSettingsPopup(mod, false);
        if (!fresh)
            return;
        EbInLevelSettingsFilterKeeper::attach(fresh);
        if (auto* keeper = EbSettingsFilterKeeperNode::findOpen()) {
            auto const category = static_cast<SettingsCategory>(restoreI);
            if (category >= SettingsCategory::Main && category < SettingsCategory::Count) {
                keeper->setCategory(category);
            }
        }
    });
}

void EbInLevelSettingsFilterKeeper::startWatchdog() {
    auto& slot = EbSettingsTabWatchdogState::slot();
    if (!slot) {
        slot = EbSettingsTabWatchdog::create();
    }
    if (!slot)
        return;
    auto* scene = geode::prelude::CCScene::get();
    if (!scene) {
        geode::prelude::Loader::get()->queueInMainThread(
            [] { EbInLevelSettingsFilterKeeper::startWatchdog(); });
        return;
    }
    slot->ensureOnScene();
}
