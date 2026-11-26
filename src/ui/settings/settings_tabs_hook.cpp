#include "settings_tabs_hook.hpp"
#include "in_level_settings_filter.hpp"
#include "settings_popup_policy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

auto EbSettingsPopupAttach::nodeTreeHasOurSetting(geode::prelude::CCNode* root) -> bool {
    if (!root || !geode::prelude::Mod::get())
        return false;
    auto* ourMod = geode::prelude::Mod::get();
    std::string const ourId = ourMod->getID();

    bool hasOwnedSettings = false;
    bool hasForeignSettings = false;
    std::vector<std::string> labels;
    std::vector<geode::prelude::CCNode*> stack{root};
    while (!stack.empty()) {
        auto* node = stack.back();
        stack.pop_back();
        if (!node)
            continue;
        // Only inspect this popup, never an alert opened inside it.
        if (node != root && geode::prelude::typeinfo_cast<FLAlertLayer*>(node))
            continue;
        if (auto* settingNode =
                geode::prelude::typeinfo_cast<geode::prelude::SettingNodeV3*>(node)) {
            if (auto setting = settingNode->getSetting()) {
                if (setting->getModID() == ourId)
                    hasOwnedSettings = true;
                else
                    hasForeignSettings = true;
            }
            continue;
        }
        if (auto* label = geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(node))
            labels.emplace_back(label->getString());
        if (auto* children = node->getChildren()) {
            for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(children))
                stack.push_back(child);
        }
    }
    for (auto const& title : labels) {
        if (EbSettingsPopupPolicy::ownsPopup(title, ourMod->getName(), hasOwnedSettings,
                                             hasForeignSettings))
            return true;
    }
    return false;
}

auto EbSettingsPopupAttach::tryAttachTabs(FLAlertLayer* layer) -> void {
    if (!layer)
        return;
    if (layer->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
        return;
    if (!layer->getChildByIDRecursive("search-input"))
        return;
    if (!nodeTreeHasOurSetting(layer))
        return;
    EbInLevelSettingsFilterKeeper::attach(layer);
}

auto EbSettingsFLAlertHook::show() -> void {
    FLAlertLayer::show();
    // Settings finish building children after show — attach over the next frames.
    auto self = geode::prelude::Ref(this);
    geode::prelude::Loader::get()->queueInMainThread([self] {
        if (!self)
            return;
        EbSettingsPopupAttach::tryAttachTabs(self.data());
        geode::prelude::Loader::get()->queueInMainThread([self] {
            if (!self)
                return;
            EbSettingsPopupAttach::tryAttachTabs(self.data());
            geode::prelude::Loader::get()->queueInMainThread([self] {
                if (self)
                    EbSettingsPopupAttach::tryAttachTabs(self.data());
            });
        });
    });
}
