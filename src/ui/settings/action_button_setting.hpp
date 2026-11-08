#ifndef ACTION_BUTTON_SETTING_HPP
#define ACTION_BUTTON_SETTING_HPP

#include <Geode/loader/SettingV3.hpp>
#include <string>

class ButtonSprite;
class CCMenuItemSpriteExtra;

// Geode custom setting: same row chrome as bool (label + i), button on the right.
class ActionButtonSettingV3 : public geode::SettingV3 {
public:
    static geode::Result<std::shared_ptr<geode::SettingV3>>
    parse(std::string key, std::string modID, matjson::Value const& json);

    std::string const& buttonText() const {
        return this->m_buttonText;
    }

    bool load(matjson::Value const& json) override;
    bool save(matjson::Value& json) const override;
    bool isDefaultValue() const override;
    void reset() override;
    geode::SettingNodeV3* createNode(float width) override;

private:
    std::string m_buttonText = "Go";
};

// Call from $execute and $on_mod(Loaded) — createSettings only fills types registered in time.

class ActionButtonSettingNodeV3 : public geode::SettingNodeV3 {
public:
    bool hasUncommittedChanges() const override {
        return false;
    }
    bool hasNonDefaultValue() const override {
        return false;
    }

    static ActionButtonSettingNodeV3* create(std::shared_ptr<ActionButtonSettingV3> setting,
                                             float width);

protected:
    bool init(std::shared_ptr<ActionButtonSettingV3> setting, float width);
    void updateState(cocos2d::CCNode* invoker) override;
    void onCommit() override {}
    void onResetToDefault() override {}
    void onButton(cocos2d::CCObject*);
    void onTick(float);
    void refreshLabel();

private:
    ButtonSprite* m_buttonSprite = nullptr;
    CCMenuItemSpriteExtra* m_button = nullptr;
    std::string m_shownLabel;
};

#include "../../config/stage_config.hpp"
#include "../../core/bake/bake_map.hpp"
#include "../../core/bake/bake_scheduler.hpp"
#include "../../debug/assist_log.hpp"
#include "../pause/pause_action_bridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <fmt/format.h>

class ActionButtonSettingImplementation {
public:
    static std::string truncateLabel(std::string text, size_t maxChars);
    static std::string dynamicButtonLabel(std::string const& key, std::string const& fallback);
    static bool actionShouldEnable(std::string const& key);
    static void fireSettingAction(std::string const& key);
    static void registerActionButtonSettingType();
    static void fitActionButtonSprite(ButtonSprite* sprite);
    static void layoutActionButton(geode::prelude::CCMenu* menu, CCMenuItemSpriteExtra* button,
                                   ButtonSprite* sprite);

private:
    friend class ActionButtonSettingV3;
    friend class ActionButtonSettingNodeV3;

    inline static constexpr float kActionBtnMaxW = 88.f;
    inline static constexpr float kActionBtnScale = 0.4f;

    ActionButtonSettingImplementation() = delete;
};

#endif
