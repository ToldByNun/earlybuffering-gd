#include "settings_tab_watchdog.hpp"
#include "../filtering/in_level_settings_filter_keeper.hpp"
#include "../filtering/settings_filter_walk.hpp"
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
    if (!root || !geode::prelude::Mod::get())
        return false;

    std::string const ourId = geode::prelude::Mod::get()->getID();
    std::vector<geode::prelude::SettingNodeV3*> settings;
    geode::prelude::ScrollLayer* list = nullptr;
    EbSettingsFilterWalk::collect(root, settings, list);
    for (auto* sett : settings) {
        if (!sett)
            continue;
        if (auto s = sett->getSetting()) {
            if (s->getModID() == ourId)
                return true;
        }
        auto* label = sett->getNameLabel();
        if (!label)
            continue;
        std::string const text = label->getString();
        if (text.find("import-bake-replay") != std::string::npos ||
            text.find("bake-map-action") != std::string::npos ||
            text.find("show-pause-hub") != std::string::npos) {
            return true;
        }
    }

    std::vector<geode::prelude::CCNode*> stack{root};
    while (!stack.empty()) {
        auto* count = stack.back();
        stack.pop_back();
        if (!count)
            continue;

        if (auto* lab = geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(count)) {
            std::string const text = lab->getString();
            if (text.find("EarlyBuffer") != std::string::npos &&
                text.find("Settings") != std::string::npos) {
                return true;
            }
        }
        auto* kids = count->getChildren();
        if (!kids)
            continue;
        for (auto* candidate : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(kids))
            stack.push_back(candidate);
    }
    return false;
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
