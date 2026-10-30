#include "InLevelSettingsFilter.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

using namespace geode::prelude;

// File-local attach helpers (class, not a project / anonymous namespace).
class SettingsPopupAttach {
public:
    static bool nodeTreeHasOurSetting(CCNode* root) {
        if (!root || !Mod::get()) return false;
        auto* ourMod = Mod::get();
        std::string const ourId = ourMod->getID();

        std::vector<CCNode*> stack{ root };
        while (!stack.empty()) {
            auto* node = stack.back();
            stack.pop_back();
            if (!node) continue;

            if (auto* sett = typeinfo_cast<SettingNodeV3*>(node)) {
                if (auto s = sett->getSetting()) {
                    if (s->getModID() == ourId || s->getMod() == ourMod) return true;
                }
                if (auto* label = sett->getNameLabel()) {
                    std::string const text = label->getString();
                    if (text.find("import-bake-replay") != std::string::npos
                        || text.find("bake-map-action") != std::string::npos
                        || text.find("show-pause-hub") != std::string::npos) {
                        return true;
                    }
                }
                continue;
            }

            if (auto* lab = typeinfo_cast<CCLabelBMFont*>(node)) {
                std::string const t = lab->getString();
                if (t.find("EarlyBuffer") != std::string::npos
                    && (t.find("Settings") != std::string::npos
                        || t.find("SETTINGS") != std::string::npos)) {
                    return true;
                }
            }

            if (auto* kids = node->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(kids)) {
                    stack.push_back(child);
                }
            }
        }
        return false;
    }

    static void tryAttachTabs(FLAlertLayer* layer) {
        if (!layer) return;
        if (layer->getChildByID("earlybuffer-inlevel-settings-filter"_spr)) return;
        if (!layer->getChildByIDRecursive("search-input")) return;
        if (!nodeTreeHasOurSetting(layer)) return;
        InLevelSettingsFilterKeeper::attach(layer);
    }

private:
    SettingsPopupAttach() = delete;
};

class $modify(EbSettingsFLAlertHook, FLAlertLayer) {
    void show() {
        FLAlertLayer::show();
        // Settings finish building children after show — attach over the next frames.
        auto self = Ref(this);
        Loader::get()->queueInMainThread([self] {
            if (!self) return;
            SettingsPopupAttach::tryAttachTabs(self.data());
            Loader::get()->queueInMainThread([self] {
                if (!self) return;
                SettingsPopupAttach::tryAttachTabs(self.data());
                Loader::get()->queueInMainThread([self] {
                    if (self) SettingsPopupAttach::tryAttachTabs(self.data());
                });
            });
        });
    }
};
