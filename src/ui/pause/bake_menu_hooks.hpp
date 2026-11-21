#ifndef BAKE_MENU_HOOKS_HPP
#define BAKE_MENU_HOOKS_HPP

class EbPauseMenuAttach;

#include "../../config/stage_config.hpp"
#include "../../core/bake/bake_scheduler.hpp"
#include "../../debug/assist_log.hpp"
#include "pause_action_bridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <string>

class EbPauseMenuAttach {
public:
    static geode::prelude::CCNode* hostLayer(PauseLayer* self);

    static geode::prelude::CCMenu* rightButtonMenu(PauseLayer* self);

    // Remove Import/Export Cfg / Cycle Preset / … leftovers from older installs.
    static void stripLegacySideButtons(PauseLayer* self);

private:
    static std::string menuItemLabel(geode::prelude::CCNode* node);
    static bool isLegacySideButtonLabel(std::string const& label);
    static void stripLegacyLabelsFromMenu(geode::prelude::CCMenu* menu);
};

class $modify(EbPauseLayer, PauseLayer) {
public:
    void customSetup();

    void onBakePoll(float);

private:
};

#endif
