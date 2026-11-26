#include "settings_tab_watchdog.hpp"
#include "../filtering/in_level_settings_filter_keeper.hpp"
#include "../filtering/settings_filter_walk.hpp"
#include "../settings_tabs_hook.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <string>
#include <vector>

auto EbSettingsTabWatchdog::popupIsOurSettings(geode::prelude::CCNode* root) -> bool {
    return EbSettingsPopupAttach::nodeTreeHasOurSetting(root);
}

auto EbSettingsTabWatchdog::collectAlerts(geode::prelude::CCNode* node,
                                          std::vector<FLAlertLayer*>& out) -> void {
    if (!node)
        return;
    if (auto* alert = geode::prelude::typeinfo_cast<FLAlertLayer*>(node)) {
        out.push_back(alert);
    }
    auto* kids = node->getChildren();
    if (!kids)
        return;
    for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(kids)) {
        collectAlerts(child, out);
    }
}

auto EbSettingsTabWatchdog::findUnattachedOurPopup() -> geode::prelude::CCNode* {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return nullptr;
    if (scene->getChildByIDRecursive("earlybuffer-inlevel-settings-filter"_spr)) {
        return nullptr;
    }

    std::vector<FLAlertLayer*> alerts;
    collectAlerts(scene, alerts);
    for (auto* alert : alerts) {
        if (!alert)
            continue;
        if (alert->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
            continue;
        if (!alert->getChildByIDRecursive("search-input"))
            continue;
        if (popupIsOurSettings(alert))
            return alert;
    }
    return nullptr;
}

auto EbSettingsTabWatchdog::ensureOnScene() -> void {
    auto* scene = geode::prelude::CCScene::get();
    if (!scene)
        return;
    if (this->getParent() == scene)
        return;

    this->retain();
    this->removeFromParentAndCleanup(false);
    scene->addChild(this);
    this->release();
}

auto EbSettingsTabWatchdog::onTick(float) -> void {
    this->ensureOnScene();
    auto* popup = findUnattachedOurPopup();
    if (!popup)
        return;
    EbInLevelSettingsFilterKeeper::attach(popup);
}

auto EbSettingsTabWatchdog::initWatchdog() -> bool {
    if (!geode::prelude::CCNode::init())
        return false;
    this->setID("earlybuffer-settings-tab-watchdog"_spr);
    this->schedule(schedule_selector(EbSettingsTabWatchdog::onTick), 0.1f);
    return true;
}

auto EbSettingsTabWatchdog::create() -> EbSettingsTabWatchdog* {
    auto* createdObject = new EbSettingsTabWatchdog();
    if (!createdObject || !createdObject->initWatchdog()) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

auto EbSettingsTabWatchdogState::slot() -> geode::prelude::Ref<EbSettingsTabWatchdog>& {
    static geode::prelude::Ref<EbSettingsTabWatchdog> s_watchdog;
    return s_watchdog;
}
