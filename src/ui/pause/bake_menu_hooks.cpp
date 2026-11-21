#include "bake_menu_hooks.hpp"
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

// Resolve geode.node-ids PauseLayer menus (live under main-layer, not PauseLayer root).

auto EbPauseMenuAttach::hostLayer(PauseLayer* self) -> geode::prelude::CCNode* {
    if (!self)
        return nullptr;
    if (auto* main = self->getChildByID("main-layer"))
        return main;
    return self;
}

auto EbPauseMenuAttach::rightButtonMenu(PauseLayer* self) -> geode::prelude::CCMenu* {
    if (!(!self)) {
        return geode::prelude::typeinfo_cast<geode::prelude::CCMenu*>(
            self->getChildByIDRecursive("right-button-menu"));
    }
    return nullptr;
}

auto EbPauseMenuAttach::menuItemLabel(geode::prelude::CCNode* node) -> std::string {
    auto* item = geode::prelude::typeinfo_cast<CCMenuItemSpriteExtra*>(node);
    if (!item)
        return {};
    auto* sprite = geode::prelude::typeinfo_cast<ButtonSprite*>(item->getNormalImage());
    if (!sprite || !sprite->m_label)
        return {};
    if (auto const* text = sprite->m_label->getString())
        return text;
    return {};
}

auto EbPauseMenuAttach::isLegacySideButtonLabel(std::string const& label) -> bool {
    return label == "Import Cfg" || label == "Export Cfg" || label == "Trajectory" ||
           label == "Toggle Learn" || label == "Reset Learn" || label == "Toggle Debug" ||
           label == "Cycle Preset" || label == "Cycle Mode" || label == "Import Replay" ||
           label == "Bake Map" || label == "Brute Bake" || label == "Cancel";
}

auto EbPauseMenuAttach::stripLegacyLabelsFromMenu(geode::prelude::CCMenu* menu) -> void {
    if (!menu)
        return;
    auto* children = menu->getChildren();
    if (!children)
        return;
    for (int index = static_cast<int>(children->count()) - 1; index >= 0; --index) {
        auto* child = static_cast<geode::prelude::CCNode*>(children->objectAtIndex(index));
        if (EbPauseMenuAttach::isLegacySideButtonLabel(EbPauseMenuAttach::menuItemLabel(child)))
            menu->removeChild(child, true);
    }
    menu->updateLayout();
}

auto EbPauseMenuAttach::stripLegacySideButtons(PauseLayer* self) -> void {
    if (!self)
        return;
    if (auto* host = EbPauseMenuAttach::hostLayer(self)) {
        for (char const* id : {"earlybuffer-assist-menu"_spr, "earlybuffer-bake-menu"_spr}) {
            if (auto* leftover = host->getChildByID(id))
                leftover->removeFromParentAndCleanup(true);
        }
    }
    EbPauseMenuAttach::stripLegacyLabelsFromMenu(EbPauseMenuAttach::rightButtonMenu(self));
    if (auto* left = geode::prelude::typeinfo_cast<geode::prelude::CCMenu*>(
            self->getChildByIDRecursive("left-button-menu"))) {
        EbPauseMenuAttach::stripLegacyLabelsFromMenu(left);
    }
}

auto EbPauseLayer::customSetup() -> void {
    PauseLayer::customSetup();

    // Clean pause: EB opens Geode settings (category pages). Optional status lines only.
    // Bake / Assist action buttons live under EB → Bake / Assist — no side stacks.
    EbPauseMenuAttach::stripLegacySideButtons(this);
    bool const showStatus = EbStageConfig::showPauseBake();
    bool const showHub = EbStageConfig::showPauseHub();
    if (!showStatus && !showHub)
        return;

    // Already installed (idempotent). Still (re)schedule poll — prior early-return skipped it.
    if (this->getChildByIDRecursive("earlybuffer-pause-ui"_spr)) {
        EbPauseActionBridge::refreshPauseLabels(this);
        this->schedule(schedule_selector(EbPauseLayer::onBakePoll), 0.15f);
        return;
    }

    auto* host = EbPauseMenuAttach::hostLayer(this);
    if (!host)
        return;

    auto win = this->getContentSize();
    auto* marker = geode::prelude::CCNode::create();
    marker->setID("earlybuffer-pause-ui"_spr);
    host->addChild(marker);

    // Top-right: opens desktop ImGui shell / Geode settings (Main/Assist/Learn/Bake/…).
    if (showHub) {
        auto* sprite = geode::prelude::CircleButtonSprite::create(
            geode::prelude::CCLabelBMFont::create("EB", "bigFont.fnt"),
            geode::prelude::CircleBaseColor::Cyan, geode::prelude::CircleBaseSize::Small);
        if (!sprite) {
            sprite = geode::prelude::CircleButtonSprite::createWithSpriteFrameName(
                "GJ_optionsBtn_001.png", 0.6f, geode::prelude::CircleBaseColor::Cyan,
                geode::prelude::CircleBaseSize::Small);
        }
        auto* buttonValue = geode::prelude::CCMenuItemExt::createSpriteExtra(
            sprite, [](CCMenuItemSpriteExtra*) { EbPauseActionBridge::openHub(); });
        buttonValue->setID("earlybuffer-settings-btn"_spr);
        buttonValue->setScale(0.85f);

        if (auto* right = EbPauseMenuAttach::rightButtonMenu(this)) {
            right->addChild(buttonValue);
            right->updateLayout();
            EbAssistLog::debug("pause-ui: EB settings on right-button-menu");
        } else {
            auto* menu = geode::prelude::CCMenu::create();
            menu->setID("earlybuffer-settings-menu"_spr);
            menu->ignoreAnchorPointForPosition(false);
            menu->setAnchorPoint({1.f, 1.f});
            menu->setPosition({win.width - 12.f, win.height - 12.f});
            menu->addChild(buttonValue);
            host->addChild(menu, 1100);
            EbAssistLog::debug("pause-ui: EB settings fallback top-right menu");
        }
    }

    if (showStatus) {
        auto* title = geode::prelude::CCLabelBMFont::create("EarlyBuffer", "goldFont.fnt");
        if (!title)
            title = geode::prelude::CCLabelBMFont::create("EarlyBuffer", "bigFont.fnt");
        title->setID("earlybuffer-bake-title"_spr);
        title->setScale(0.45f);
        title->setAnchorPoint({0.f, 1.f});
        title->setPosition({14.f, win.height - 52.f});
        host->addChild(title, 999);

        auto* modeLbl = geode::prelude::CCLabelBMFont::create("Mode: …", "chatFont.fnt");
        if (!modeLbl)
            modeLbl = geode::prelude::CCLabelBMFont::create("Mode: …", "bigFont.fnt");
        modeLbl->setID("earlybuffer-mode-label"_spr);
        modeLbl->setScale(0.5f);
        modeLbl->setAnchorPoint({0.f, 1.f});
        modeLbl->setPosition({14.f, win.height - 72.f});
        host->addChild(modeLbl, 999);

        auto* status = geode::prelude::CCLabelBMFont::create("…", "chatFont.fnt");
        if (!status)
            status = geode::prelude::CCLabelBMFont::create("…", "bigFont.fnt");
        status->setID("earlybuffer-bake-status"_spr);
        status->setScale(0.45f);
        status->setAnchorPoint({0.f, 1.f});
        status->setOpacity(210);
        status->setPosition({14.f, win.height - 92.f});
        host->addChild(status, 999);
    }

    EbPauseActionBridge::refreshPauseLabels(this);
    this->schedule(schedule_selector(EbPauseLayer::onBakePoll), 0.15f);
}

auto EbPauseLayer::onBakePoll(float) -> void {
    EbPauseActionBridge::refreshPauseLabels(this);
    EbPauseActionBridge::refreshOpenSettings();
    EbBakeScheduler::get().tickSlice();
}
