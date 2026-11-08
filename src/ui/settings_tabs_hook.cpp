#include "settings_tabs_hook.hpp"
#include "in_level_settings_filter.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

auto SettingsPopupAttach::nodeTreeHasOurSetting(geode::prelude::CCNode* root) -> bool {
    if (!root || !geode::prelude::Mod::get())
        return false;
    auto* ourMod = geode::prelude::Mod::get();
    std::string const ourId = ourMod->getID();

    std::vector<geode::prelude::CCNode*> stack{root};
    while (!stack.empty()) {
        auto* node = stack.back();
        stack.pop_back();
        if (!node)
            continue;

        if (auto* sett = geode::prelude::typeinfo_cast<geode::prelude::SettingNodeV3*>(node)) {
            if (auto s = sett->getSetting()) {
                if (s->getModID() == ourId || s->getMod() == ourMod)
                    return true;
            }
            if (auto* label = sett->getNameLabel()) {
                std::string const text = label->getString();
                if (text.find("import-bake-replay") != std::string::npos ||
                    text.find("bake-map-action") != std::string::npos ||
                    text.find("show-pause-hub") != std::string::npos) {
                    return true;
                }
            }
            continue;
        }

        if (auto* lab = geode::prelude::typeinfo_cast<geode::prelude::CCLabelBMFont*>(node)) {
            std::string const text = lab->getString();
            if (text.find("EarlyBuffer") != std::string::npos &&
                (text.find("Settings") != std::string::npos ||
                 text.find("SETTINGS") != std::string::npos)) {
                return true;
            }
        }

        if (auto* kids = node->getChildren()) {
            for (auto* child : geode::prelude::CCArrayExt<geode::prelude::CCNode*>(kids)) {
                stack.push_back(child);
            }
        }
    }
    return false;
}

auto SettingsPopupAttach::tryAttachTabs(FLAlertLayer* layer) -> void {
    if (!layer)
        return;
    if (layer->getChildByID("earlybuffer-inlevel-settings-filter"_spr))
        return;
    if (!layer->getChildByIDRecursive("search-input"))
        return;
    if (!nodeTreeHasOurSetting(layer))
        return;
    InLevelSettingsFilterKeeper::attach(layer);
}

auto EbSettingsFLAlertHook::show() -> void {
    FLAlertLayer::show();
    // Settings finish building children after show — attach over the next frames.
    auto self = geode::prelude::Ref(this);
    geode::prelude::Loader::get()->queueInMainThread([self] {
        if (!self)
            return;
        SettingsPopupAttach::tryAttachTabs(self.data());
        geode::prelude::Loader::get()->queueInMainThread([self] {
            if (!self)
                return;
            SettingsPopupAttach::tryAttachTabs(self.data());
            geode::prelude::Loader::get()->queueInMainThread([self] {
                if (self)
                    SettingsPopupAttach::tryAttachTabs(self.data());
            });
        });
    });
}
