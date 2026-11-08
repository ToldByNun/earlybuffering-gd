#include "action_button_setting.hpp"
#include "../config/stage_config.hpp"
#include "../core/bake_map.hpp"
#include "../core/bake_scheduler.hpp"
#include "../debug/assist_log.hpp"
#include "pause_action_bridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <fmt/format.h>

std::string ActionButtonSettingImplementation::truncateLabel(std::string text, size_t maxChars) {
    if (text.size() <= maxChars)
        return text;
    if (!(maxChars <= 3)) {
        return text.substr(0, maxChars - 3) + "...";
    }
    return text.substr(0, maxChars);
}

std::string ActionButtonSettingImplementation::dynamicButtonLabel(std::string const& key,
                                                                  std::string const& fallback) {
    if (key == "import-bake-replay") {
        if (auto const* r = BakeMap::get().pendingReplay()) {
            return ActionButtonSettingImplementation::truncateLabel(r->sourceName, 14);
        }
    }
    if (key == "cycle-assist-mode") {
        return ActionButtonSettingImplementation::truncateLabel(
            StageConfig::assistModeName(StageConfig::assistMode()), 12);
    }
    return fallback;
}

bool ActionButtonSettingImplementation::actionShouldEnable(std::string const& key) {
    bool const busy = BakeMap::get().isBaking() || BakeScheduler::get().running();
    if (key == "bake-map-action" || key == "brute-bake-action" || key == "import-bake-replay") {
        return !busy;
    }
    if (key == "cancel-bake-action") {
        return busy;
    }
    return true;
}

void ActionButtonSettingImplementation::fireSettingAction(std::string const& key) {
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
    if (key == "reset-learn-action") {
        PauseActionBridge::resetLearning();
        return;
    }
    if (key == "export-config-action") {
        PauseActionBridge::exportConfig();
        return;
    }
    if (key == "browse-cloud-configs-action") {
        PauseActionBridge::openCloudConfigs();
        return;
    }
    if (key == "import-config-action") {
        PauseActionBridge::importConfig();
        return;
    }
    if (key == "dev-start-record-action") {
        PauseActionBridge::devStartRecord();
        return;
    }
    if (key == "dev-stop-record-action") {
        PauseActionBridge::devStopRecord();
        return;
    }
    if (key == "dev-eval-last-action") {
        PauseActionBridge::devEvalLast();
        return;
    }
    if (key == "dev-eval-session-action") {
        PauseActionBridge::devEvalSession();
        return;
    }
    if (key == "dev-export-session-action") {
        PauseActionBridge::devExportSession();
        return;
    }
    if (key == "dev-clear-traces-action") {
        PauseActionBridge::devClearTraces();
        return;
    }
    if (key == "dev-copy-report-action") {
        PauseActionBridge::devCopyReport();
        return;
    }
    if (key == "dev-dump-diag-action") {
        PauseActionBridge::devDumpDiag();
        return;
    }
}

geode::Result<std::shared_ptr<geode::prelude::SettingV3>>
ActionButtonSettingV3::parse(std::string key, std::string modID, matjson::Value const& json) {
    auto result = std::make_shared<ActionButtonSettingV3>();
    auto root = geode::prelude::checkJson(json, "ActionButtonSettingV3");
    // Same pattern as Geode docs button example (avoid parseBaseProperties / requires-restart).
    result->init(key, modID, root);
    result->parseNameAndDescription(root);
    result->parseEnableIf(root);
    root.has("button-text").into(result->m_buttonText);
    // Do not checkUnknownKeys — extra mod.json fields must not kill the whole setting.
    return root.ok(std::static_pointer_cast<geode::prelude::SettingV3>(result));
}

void ActionButtonSettingImplementation::registerActionButtonSettingType() {
    static bool s_done = false;
    if (s_done)
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    auto result = mod->registerCustomSettingType("eb-action", &ActionButtonSettingV3::parse);
    if (!result) {
        geode::log::error("EarlyBuffer: eb-action register failed: {}", result.unwrapErr());
        AssistLog::info(fmt::format("WARN eb-action register: {}", result.unwrapErr()));
        return;
    }
    s_done = true;
    if (mod->getSetting("import-bake-replay")) {
        AssistLog::info("eb-action settings OK (import-bake-replay resolved)");
    } else {
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

geode::prelude::SettingNodeV3* ActionButtonSettingV3::createNode(float width) {
    return ActionButtonSettingNodeV3::create(
        std::static_pointer_cast<ActionButtonSettingV3>(shared_from_this()), width);
}

// Cap button width so long filenames don't eat the label column.

void ActionButtonSettingImplementation::fitActionButtonSprite(ButtonSprite* sprite) {
    if (!sprite)
        return;
    sprite->setScale(ActionButtonSettingImplementation::kActionBtnScale);
    float const sprW = sprite->getContentSize().width * sprite->getScale();
    if (sprW > ActionButtonSettingImplementation::kActionBtnMaxW && sprW > 0.f) {
        sprite->setScale(ActionButtonSettingImplementation::kActionBtnMaxW /
                         sprite->getContentSize().width);
    }
}

// Mirror BoolSettingNodeV3: shrink button-menu, place control at geode::Anchor::Right with
// half-width offset so the visual right edge sits flush (default center anchor).
// Do NOT touch the button's contentSize/anchorPoint — that desyncs the ButtonSprite.
void ActionButtonSettingImplementation::layoutActionButton(geode::prelude::CCMenu* menu,
                                                           CCMenuItemSpriteExtra* button,
                                                           ButtonSprite* sprite) {
    if (!menu || !button || !sprite)
        return;
    ActionButtonSettingImplementation::fitActionButtonSprite(sprite);
    float const btnW = sprite->getContentSize().width * sprite->getScale();
    // Same flush as checkboxes (menu already at row Right, ccp(-10,0)).
    menu->setContentWidth(std::max(btnW + 4.f, 24.f));
    geode::prelude::CCPoint const offset = ccp(-btnW * 0.5f, 0.f);
    if (button->getParent() != menu) {
        menu->addChildAtPosition(button, geode::Anchor::Right, offset);
    } else if (auto* opts = geode::prelude::typeinfo_cast<geode::AnchorLayoutOptions*>(
                   button->getLayoutOptions())) {
        opts->setAnchor(geode::Anchor::Right);
        opts->setOffset(offset);
        button->updateAnchoredPosition(geode::Anchor::Right, offset);
    } else {
        button->removeFromParent();
        menu->addChildAtPosition(button, geode::Anchor::Right, offset);
    }
    menu->updateLayout();
}

bool ActionButtonSettingNodeV3::init(std::shared_ptr<ActionButtonSettingV3> setting, float width) {
    if (!geode::prelude::SettingNodeV3::init(setting, width))
        return false;

    auto* menu = this->getButtonMenu();
    auto const initial = ActionButtonSettingImplementation::dynamicButtonLabel(
        setting->getKey(), setting->buttonText());
    this->m_shownLabel = initial;
    this->m_buttonSprite =
        ButtonSprite::create(initial.c_str(), "bigFont.fnt", "GJ_button_01.png", 0.8f);
    this->m_button = CCMenuItemSpriteExtra::create(
        this->m_buttonSprite, this, menu_selector(ActionButtonSettingNodeV3::onButton));
    ActionButtonSettingImplementation::layoutActionButton(menu, this->m_button,
                                                          this->m_buttonSprite);

    this->schedule(schedule_selector(ActionButtonSettingNodeV3::onTick), 0.2f);
    this->updateState(nullptr);
    return true;
}

void ActionButtonSettingNodeV3::refreshLabel() {
    auto setting = std::static_pointer_cast<ActionButtonSettingV3>(this->getSetting());
    if (!setting)
        return;
    if (!this->m_buttonSprite || !this->m_button || !this->getButtonMenu())
        return;

    auto const next = ActionButtonSettingImplementation::dynamicButtonLabel(setting->getKey(),
                                                                            setting->buttonText());
    if (next == this->m_shownLabel)
        return;

    this->m_shownLabel = next;
    this->m_buttonSprite->setString(next.c_str());
    ActionButtonSettingImplementation::layoutActionButton(this->getButtonMenu(), this->m_button,
                                                          this->m_buttonSprite);
}

void ActionButtonSettingNodeV3::updateState(geode::prelude::CCNode* invoker) {
    geode::prelude::SettingNodeV3::updateState(invoker);
    this->refreshLabel();

    auto setting = this->getSetting();
    bool const enable = setting && setting->shouldEnable() &&
                        ActionButtonSettingImplementation::actionShouldEnable(setting->getKey());

    if (this->m_button)
        this->m_button->setEnabled(enable);
    if (!this->m_buttonSprite)
        return;

    this->m_buttonSprite->setCascadeColorEnabled(true);
    this->m_buttonSprite->setCascadeOpacityEnabled(true);
    this->m_buttonSprite->setOpacity(enable ? 255 : 155);
    this->m_buttonSprite->setColor(enable ? geode::prelude::ccWHITE : geode::prelude::ccGRAY);
}

void ActionButtonSettingNodeV3::onTick(float) {
    this->refreshLabel();

    auto setting = this->getSetting();
    if (!setting || !this->m_button)
        return;

    bool const enable = setting->shouldEnable() &&
                        ActionButtonSettingImplementation::actionShouldEnable(setting->getKey());
    this->m_button->setEnabled(enable);
    if (!this->m_buttonSprite)
        return;

    this->m_buttonSprite->setOpacity(enable ? 255 : 155);
    this->m_buttonSprite->setColor(enable ? geode::prelude::ccWHITE : geode::prelude::ccGRAY);
}

void ActionButtonSettingNodeV3::onButton(geode::prelude::CCObject*) {
    auto setting = this->getSetting();
    if (!setting)
        return;

    ActionButtonSettingImplementation::fireSettingAction(setting->getKey());
    this->refreshLabel();
    this->updateState(nullptr);
}

ActionButtonSettingNodeV3*
ActionButtonSettingNodeV3::create(std::shared_ptr<ActionButtonSettingV3> setting, float width) {
    auto* createdObject = new ActionButtonSettingNodeV3();
    if (!createdObject || !createdObject->init(setting, width)) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

$execute {
    ActionButtonSettingImplementation::registerActionButtonSettingType();
}
