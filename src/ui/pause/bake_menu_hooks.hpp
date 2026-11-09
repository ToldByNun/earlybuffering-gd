#ifndef BAKE_MENU_HOOKS_HPP
#define BAKE_MENU_HOOKS_HPP

class EbPauseMenuAttach;

#include "../../config/stage_config.hpp"
#include "../../core/bake/bake_scheduler.hpp"
#include "../../debug/assist_log.hpp"
#include "pause_action_bridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>

class EbPauseMenuAttach {
public:
    static geode::prelude::CCNode* hostLayer(PauseLayer* self);

    static geode::prelude::CCMenu* rightButtonMenu(PauseLayer* self);

private:
};

class $modify(EbPauseLayer, PauseLayer) {
public:
    void customSetup();

    void onBakePoll(float);

private:
};

#endif
