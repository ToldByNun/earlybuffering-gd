#ifndef SETTINGS_IMPORT_UI_SYNC_HPP
#define SETTINGS_IMPORT_UI_SYNC_HPP

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cocos-ext.h>
#include <fmt/format.h>
#include <string>

class EbSettingsImportUiSync {
public:
    template <class SettingT> static bool syncValueNode(geode::prelude::SettingNodeV3* sett) {
        auto* node =
            geode::prelude::typeinfo_cast<geode::prelude::SettingValueNodeV3<SettingT>*>(sett);
        if (!node)
            return false;
        node->setValue(node->getSetting()->getValue(), nullptr);
        return true;
    }

    static void syncNodesFromStore(geode::prelude::CCNode* root, std::string const& ourId);

    static geode::prelude::CCNode* findOurSettingsPopup(geode::prelude::CCNode* scene,
                                                        std::string const& ourId);

private:
    EbSettingsImportUiSync() = delete;
};

#endif
