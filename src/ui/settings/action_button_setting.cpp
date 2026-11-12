#include "action_button_setting.hpp"
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

std::string EbActionButtonSettingImplementation::truncateLabel(std::string text, size_t maxChars) {
    if (text.size() <= maxChars)
        return text;
    if (!(maxChars <= 3)) {
        return text.substr(0, maxChars - 3) + "...";
    }
    return text.substr(0, maxChars);
}

std::string EbActionButtonSettingImplementation::dynamicButtonLabel(std::string const& key,
                                                                  std::string const& fallback) {
    if (key == "import-bake-replay") {
        if (auto const* r = EbBakeMap::get().pendingReplay()) {
            return EbActionButtonSettingImplementation::truncateLabel(r->sourceName, 14);
        }
    }
    if (key == "cycle-assist-mode") {
        return EbActionButtonSettingImplementation::truncateLabel(
            EbStageConfig::assistModeName(EbStageConfig::assistMode()), 12);
    }
    return fallback;
}

bool EbActionButtonSettingImplementation::actionShouldEnable(std::string const& key) {
    bool const busy = EbBakeMap::get().isBaking() || EbBakeScheduler::get().running();
    if (key == "bake-map-action" || key == "brute-bake-action" || key == "import-bake-replay") {
        return !busy;
    }
    if (key == "cancel-bake-action") {
        return busy;
    }
    return true;
}

void EbActionButtonSettingImplementation::fireSettingAction(std::string const& key) {
    if (key == "import-bake-replay") {
        EbPauseActionBridge::importReplay();
        return;
    }
    if (key == "bake-map-action") {
        EbPauseActionBridge::startBake(false);
        return;
    }
    if (key == "brute-bake-action") {
        EbPauseActionBridge::startBake(true);
        return;
    }
    if (key == "cancel-bake-action") {
        EbPauseActionBridge::cancelBake();
        return;
    }
    if (key == "cycle-assist-mode") {
        EbPauseActionBridge::cycleAssistMode();
        return;
    }
    if (key == "reset-learn-action") {
        EbPauseActionBridge::resetLearning();
        return;
    }
    if (key == "export-config-action") {
        EbPauseActionBridge::exportConfig();
        return;
    }
    if (key == "browse-cloud-configs-action") {
        EbPauseActionBridge::openCloudConfigs();
        return;
    }
    if (key == "import-config-action") {
        EbPauseActionBridge::importConfig();
        return;
    }
    if (key == "dev-start-record-action") {
        EbPauseActionBridge::devStartRecord();
        return;
    }
    if (key == "dev-stop-record-action") {
        EbPauseActionBridge::devStopRecord();
        return;
    }
    if (key == "dev-eval-last-action") {
        EbPauseActionBridge::devEvalLast();
        return;
    }
    if (key == "dev-eval-session-action") {
        EbPauseActionBridge::devEvalSession();
        return;
    }
    if (key == "dev-export-session-action") {
        EbPauseActionBridge::devExportSession();
        return;
    }
    if (key == "dev-clear-traces-action") {
        EbPauseActionBridge::devClearTraces();
        return;
    }
    if (key == "dev-copy-report-action") {
        EbPauseActionBridge::devCopyReport();
        return;
    }
    if (key == "dev-dump-diag-action") {
        EbPauseActionBridge::devDumpDiag();
        return;
    }
    if (key == "dev-export-research-pack-action") {
        EbPauseActionBridge::devExportResearchPack();
        return;
    }
    if (key == "dev-compare-sessions-action") {
        EbPauseActionBridge::devCompareSessions();
        return;
    }
}

geode::Result<std::shared_ptr<geode::prelude::SettingV3>>
EbActionButtonSettingV3::parse(std::string key, std::string modID, matjson::Value const& json) {
    auto result = std::make_shared<EbActionButtonSettingV3>();
    auto root = geode::prelude::checkJson(json, "ActionButtonSettingV3");
    // Same pattern as Geode docs button example (avoid parseBaseProperties / requires-restart).
    result->init(key, modID, root);
    result->parseNameAndDescription(root);
    result->parseEnableIf(root);
    root.has("button-text").into(result->m_buttonText);
    // Do not checkUnknownKeys — extra mod.json fields must not kill the whole setting.
    return root.ok(std::static_pointer_cast<geode::prelude::SettingV3>(result));
}

void EbActionButtonSettingImplementation::registerActionButtonSettingType() {
    static bool s_done = false;
    if (s_done)
        return;
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    auto result = mod->registerCustomSettingType("eb-action", &EbActionButtonSettingV3::parse);
    if (!result) {
        geode::log::error("EarlyBuffer: eb-action register failed: {}", result.unwrapErr());
        EbAssistLog::info(fmt::format("WARN eb-action register: {}", result.unwrapErr()));
        return;
    }
    s_done = true;
    if (mod->getSetting("import-bake-replay")) {
        EbAssistLog::info("eb-action settings OK (import-bake-replay resolved)");
    } else {
        EbAssistLog::info("WARN eb-action: import-bake-replay still unresolved after register");
    }
}

bool EbActionButtonSettingV3::load(matjson::Value const&) {
    return true;
}

bool EbActionButtonSettingV3::save(matjson::Value&) const {
    return true;
}

bool EbActionButtonSettingV3::isDefaultValue() const {
    return true;
}

void EbActionButtonSettingV3::reset() {}

geode::prelude::SettingNodeV3* EbActionButtonSettingV3::createNode(float width) {
    return EbActionButtonSettingNodeV3::create(
        std::static_pointer_cast<EbActionButtonSettingV3>(shared_from_this()), width);
}

// Cap button width so long filenames don't eat the label column.

void EbActionButtonSettingImplementation::fitActionButtonSprite(ButtonSprite* sprite) {
    if (!sprite)
        return;
    sprite->setScale(EbActionButtonSettingImplementation::kActionBtnScale);
    float const sprW = sprite->getContentSize().width * sprite->getScale();
    if (sprW > EbActionButtonSettingImplementation::kActionBtnMaxW && sprW > 0.f) {
        sprite->setScale(EbActionButtonSettingImplementation::kActionBtnMaxW /
                         sprite->getContentSize().width);
    }
}

// Mirror BoolSettingNodeV3: shrink button-menu, place control at geode::Anchor::Right with
// half-width offset so the visual right edge sits flush (default center anchor).
// Do NOT touch the button's contentSize/anchorPoint — that desyncs the ButtonSprite.
void EbActionButtonSettingImplementation::layoutActionButton(geode::prelude::CCMenu* menu,
                                                           CCMenuItemSpriteExtra* button,
                                                           ButtonSprite* sprite) {
    if (!menu || !button || !sprite)
        return;
    EbActionButtonSettingImplementation::fitActionButtonSprite(sprite);
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

bool EbActionButtonSettingNodeV3::init(std::shared_ptr<EbActionButtonSettingV3> setting, float width) {
    if (!geode::prelude::SettingNodeV3::init(setting, width))
        return false;

    auto* menu = this->getButtonMenu();
    auto const initial = EbActionButtonSettingImplementation::dynamicButtonLabel(
        setting->getKey(), setting->buttonText());
    this->m_shownLabel = initial;
    this->m_buttonSprite =
        ButtonSprite::create(initial.c_str(), "bigFont.fnt", "GJ_button_01.png", 0.8f);
    this->m_button = CCMenuItemSpriteExtra::create(
        this->m_buttonSprite, this, menu_selector(EbActionButtonSettingNodeV3::onButton));
    EbActionButtonSettingImplementation::layoutActionButton(menu, this->m_button,
                                                          this->m_buttonSprite);

    this->schedule(schedule_selector(EbActionButtonSettingNodeV3::onTick), 0.2f);
    this->updateState(nullptr);
    return true;
}

void EbActionButtonSettingNodeV3::refreshLabel() {
    auto setting = std::static_pointer_cast<EbActionButtonSettingV3>(this->getSetting());
    if (!setting)
        return;
    if (!this->m_buttonSprite || !this->m_button || !this->getButtonMenu())
        return;

    auto const next = EbActionButtonSettingImplementation::dynamicButtonLabel(setting->getKey(),
                                                                            setting->buttonText());
    if (next == this->m_shownLabel)
        return;

    this->m_shownLabel = next;
    this->m_buttonSprite->setString(next.c_str());
    EbActionButtonSettingImplementation::layoutActionButton(this->getButtonMenu(), this->m_button,
                                                          this->m_buttonSprite);
}

void EbActionButtonSettingNodeV3::updateState(geode::prelude::CCNode* invoker) {
    geode::prelude::SettingNodeV3::updateState(invoker);
    this->refreshLabel();

    auto setting = this->getSetting();
    bool const enable = setting && setting->shouldEnable() &&
                        EbActionButtonSettingImplementation::actionShouldEnable(setting->getKey());

    if (this->m_button)
        this->m_button->setEnabled(enable);
    if (!this->m_buttonSprite)
        return;

    this->m_buttonSprite->setCascadeColorEnabled(true);
    this->m_buttonSprite->setCascadeOpacityEnabled(true);
    this->m_buttonSprite->setOpacity(enable ? 255 : 155);
    this->m_buttonSprite->setColor(enable ? geode::prelude::ccWHITE : geode::prelude::ccGRAY);
}

void EbActionButtonSettingNodeV3::onTick(float) {
    this->refreshLabel();

    auto setting = this->getSetting();
    if (!setting || !this->m_button)
        return;

    bool const enable = setting->shouldEnable() &&
                        EbActionButtonSettingImplementation::actionShouldEnable(setting->getKey());
    this->m_button->setEnabled(enable);
    if (!this->m_buttonSprite)
        return;

    this->m_buttonSprite->setOpacity(enable ? 255 : 155);
    this->m_buttonSprite->setColor(enable ? geode::prelude::ccWHITE : geode::prelude::ccGRAY);
}

void EbActionButtonSettingNodeV3::onButton(geode::prelude::CCObject*) {
    auto setting = this->getSetting();
    if (!setting)
        return;

    EbActionButtonSettingImplementation::fireSettingAction(setting->getKey());
    this->refreshLabel();
    this->updateState(nullptr);
}

EbActionButtonSettingNodeV3*
EbActionButtonSettingNodeV3::create(std::shared_ptr<EbActionButtonSettingV3> setting, float width) {
    auto* createdObject = new EbActionButtonSettingNodeV3();
    if (!createdObject || !createdObject->init(setting, width)) {
        CC_SAFE_DELETE(createdObject);
        return nullptr;
    }
    createdObject->autorelease();
    return createdObject;
}

$execute {
    EbActionButtonSettingImplementation::registerActionButtonSettingType();
}
