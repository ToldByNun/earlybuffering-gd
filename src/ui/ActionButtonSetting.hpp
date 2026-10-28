#ifndef EARLYBUFFER_UI_ACTIONBUTTONSETTING_HPP
#define EARLYBUFFER_UI_ACTIONBUTTONSETTING_HPP

#include <Geode/loader/SettingV3.hpp>
#include <string>

class ButtonSprite;
class CCMenuItemSpriteExtra;

// Geode custom setting: same row chrome as bool (label + i), button on the right.
class ActionButtonSettingV3 : public geode::SettingV3 {
public:
    static geode::Result<std::shared_ptr<geode::SettingV3>> parse(
        std::string key, std::string modID, matjson::Value const& json
    );

    std::string const& buttonText() const { return this->m_buttonText; }

    bool load(matjson::Value const& json) override;
    bool save(matjson::Value& json) const override;
    bool isDefaultValue() const override;
    void reset() override;
    geode::SettingNodeV3* createNode(float width) override;

private:
    std::string m_buttonText = "Go";
};

// Call from $execute and $on_mod(Loaded) — createSettings only fills types registered in time.
void registerActionButtonSettingType();

class ActionButtonSettingNodeV3 : public geode::SettingNodeV3 {
protected:
    bool init(std::shared_ptr<ActionButtonSettingV3> setting, float width);
    void updateState(cocos2d::CCNode* invoker) override;
    void onCommit() override {}
    void onResetToDefault() override {}
    void onButton(cocos2d::CCObject*);
    void onTick(float);
    void refreshLabel();

public:
    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }

    static ActionButtonSettingNodeV3* create(
        std::shared_ptr<ActionButtonSettingV3> setting, float width
    );

private:
    ButtonSprite* m_buttonSprite = nullptr;
    CCMenuItemSpriteExtra* m_button = nullptr;
    std::string m_shownLabel;
};

#endif // EARLYBUFFER_UI_ACTIONBUTTONSETTING_HPP
