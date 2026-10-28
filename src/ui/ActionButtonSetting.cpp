#include "ActionButtonSetting.hpp"
#include "PauseActionBridge.hpp"

#include "../core/BakeMap.hpp"
#include "../core/BakeScheduler.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <fmt/format.h>

using namespace geode::prelude;

namespace {

std::string truncateLabel(std::string s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

std::string dynamicButtonLabel(std::string const& key, std::string const& fallback) {
    if (key == "import-bake-replay") {
        if (auto const* r = BakeMap::get().pendingReplay()) {
            return truncateLabel(r->sourceName, 14);
        }
    }
    if (key == "cycle-assist-mode") {
        return truncateLabel(StageConfig::assistModeName(StageConfig::assistMode()), 12);
    }
    if (key == "cycle-preset-action") {
        return truncateLabel(StageConfig::presetName(StageConfig::stageIndex()), 10);
    }
    return fallback;
}

bool actionShouldEnable(std::string const& key) {
    bool const busy = BakeMap::get().isBaking() || BakeScheduler::get().running();
    if (key == "bake-map-action" || key == "brute-bake-action" || key == "import-bake-replay") {
        return !busy;
    }
    if (key == "cancel-bake-action") {
        return busy;
    }
    return true;
}

void fireSettingAction(std::string const& key) {
    if (key == "import-bake-replay") {
        PauseActionBridge::importReplay();
        return;
    }
    if (key == "bake-map-action") {
        PauseActionBridge::startBake(false);
        return;
    }
    if (key == "brute-bake-action") {
        PauseActionBridge::startBake(true);
        return;
    }
    if (key == "cancel-bake-action") {
        PauseActionBridge::cancelBake();
        return;
    }
    if (key == "cycle-assist-mode") {
        PauseActionBridge::cycleAssistMode();
        return;
    }
    if (key == "cycle-preset-action") {
        PauseActionBridge::cyclePreset();
        return;
    }
    if (key == "reset-learn-action") {
        PauseActionBridge::resetLearning();
        return;
    }
    if (key == "export-config-action") {
        PauseActionBridge::exportConfig();
        return;
    }
    if (key == "import-config-action") {
        PauseActionBridge::importConfig();
        return;
    }
}

} // namespace

Result<std::shared_ptr<SettingV3>> ActionButtonSettingV3::parse(
    std::string key, std::string modID, matjson::Value const& json
) {
    auto res = std::make_shared<ActionButtonSettingV3>();
    auto root = checkJson(json, "ActionButtonSettingV3");
    // Same pattern as Geode docs button example (avoid parseBaseProperties / requires-restart).
    res->init(key, modID, root);
    res->parseNameAndDescription(root);
    res->parseEnableIf(root);
    root.has("button-text").into(res->m_buttonText);
    // Do not checkUnknownKeys — extra mod.json fields must not kill the whole setting.
    return root.ok(std::static_pointer_cast<SettingV3>(res));
}

void registerActionButtonSettingType() {
    static bool s_done = false;
    if (s_done) return;
    auto* mod = Mod::get();
    if (!mod) return;
    auto res = mod->registerCustomSettingType("eb-action", &ActionButtonSettingV3::parse);
    if (!res) {
        log::error("EarlyBuffer: eb-action register failed: {}", res.unwrapErr());
        AssistLog::info(fmt::format("WARN eb-action register: {}", res.unwrapErr()));
        return;
    }
    s_done = true;
    if (mod->getSetting("import-bake-replay")) {
        AssistLog::info("eb-action settings OK (import-bake-replay resolved)");
    }
    else {
        AssistLog::info("WARN eb-action: import-bake-replay still unresolved after register");
    }
}

bool ActionButtonSettingV3::load(matjson::Value const&) {
    return true;
}

bool ActionButtonSettingV3::save(matjson::Value&) const {
    return true;
}

bool ActionButtonSettingV3::isDefaultValue() const {
    return true;
}

void ActionButtonSettingV3::reset() {}

SettingNodeV3* ActionButtonSettingV3::createNode(float width) {
    return ActionButtonSettingNodeV3::create(
        std::static_pointer_cast<ActionButtonSettingV3>(shared_from_this()), width
    );
}

namespace {

// Cap button width so long filenames don't eat the label column.
constexpr float kActionBtnMaxW = 88.f;
constexpr float kActionBtnScale = 0.4f;

void fitActionButtonSprite(ButtonSprite* spr) {
    if (!spr) return;
    spr->setScale(kActionBtnScale);
    float const sprW = spr->getContentSize().width * spr->getScale();
    if (sprW > kActionBtnMaxW && sprW > 0.f) {
        spr->setScale(kActionBtnMaxW / spr->getContentSize().width);
    }
}

// Mirror BoolSettingNodeV3: shrink button-menu, place control at Anchor::Right with
// half-width offset so the visual right edge sits flush (default center anchor).
// Do NOT touch the button's contentSize/anchorPoint — that desyncs the ButtonSprite.
void layoutActionButton(CCMenu* menu, CCMenuItemSpriteExtra* btn, ButtonSprite* spr) {
    if (!menu || !btn || !spr) return;
    fitActionButtonSprite(spr);
    float const btnW = spr->getContentSize().width * spr->getScale();
    // Same flush as checkboxes (menu already at row Right, ccp(-10,0)).
    menu->setContentWidth(std::max(btnW + 4.f, 24.f));
    CCPoint const offset = ccp(-btnW * 0.5f, 0.f);
    if (btn->getParent() != menu) {
        menu->addChildAtPosition(btn, Anchor::Right, offset);
    }
    else if (auto* opts = typeinfo_cast<AnchorLayoutOptions*>(btn->getLayoutOptions())) {
        opts->setAnchor(Anchor::Right);
        opts->setOffset(offset);
        btn->updateAnchoredPosition(Anchor::Right, offset);
    }
    else {
        btn->removeFromParent();
        menu->addChildAtPosition(btn, Anchor::Right, offset);
    }
    menu->updateLayout();
}

} // namespace

bool ActionButtonSettingNodeV3::init(std::shared_ptr<ActionButtonSettingV3> setting, float width) {
    if (!SettingNodeV3::init(setting, width)) return false;

    auto* menu = this->getButtonMenu();
    auto const initial = dynamicButtonLabel(setting->getKey(), setting->buttonText());
    m_shownLabel = initial;
    m_buttonSprite = ButtonSprite::create(initial.c_str(), "bigFont.fnt", "GJ_button_01.png", 0.8f);
    m_button = CCMenuItemSpriteExtra::create(
        m_buttonSprite, this, menu_selector(ActionButtonSettingNodeV3::onButton)
    );
    layoutActionButton(menu, m_button, m_buttonSprite);

    this->schedule(schedule_selector(ActionButtonSettingNodeV3::onTick), 0.2f);
    this->updateState(nullptr);
    return true;
}

void ActionButtonSettingNodeV3::refreshLabel() {
    auto setting = std::static_pointer_cast<ActionButtonSettingV3>(this->getSetting());
    if (!setting || !m_buttonSprite || !m_button || !this->getButtonMenu()) return;
    auto const next = dynamicButtonLabel(setting->getKey(), setting->buttonText());
    if (next == m_shownLabel) return;
    m_shownLabel = next;
    m_buttonSprite->setString(next.c_str());
    layoutActionButton(this->getButtonMenu(), m_button, m_buttonSprite);
}

void ActionButtonSettingNodeV3::updateState(CCNode* invoker) {
    SettingNodeV3::updateState(invoker);
    this->refreshLabel();
    auto setting = this->getSetting();
    bool const enable = setting && setting->shouldEnable() && actionShouldEnable(setting->getKey());
    if (m_button) m_button->setEnabled(enable);
    if (m_buttonSprite) {
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setOpacity(enable ? 255 : 155);
        m_buttonSprite->setColor(enable ? ccWHITE : ccGRAY);
    }
}

void ActionButtonSettingNodeV3::onTick(float) {
    this->refreshLabel();
    auto setting = this->getSetting();
    if (!setting || !m_button) return;
    bool const enable = setting->shouldEnable() && actionShouldEnable(setting->getKey());
    m_button->setEnabled(enable);
    if (m_buttonSprite) {
        m_buttonSprite->setOpacity(enable ? 255 : 155);
        m_buttonSprite->setColor(enable ? ccWHITE : ccGRAY);
    }
}

void ActionButtonSettingNodeV3::onButton(CCObject*) {
    auto setting = this->getSetting();
    if (!setting) return;
    fireSettingAction(setting->getKey());
    this->refreshLabel();
    this->updateState(nullptr);
}

ActionButtonSettingNodeV3* ActionButtonSettingNodeV3::create(
    std::shared_ptr<ActionButtonSettingV3> setting, float width
) {
    auto* ret = new ActionButtonSettingNodeV3();
    if (ret && ret->init(setting, width)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

$execute {
    registerActionButtonSettingType();
}
