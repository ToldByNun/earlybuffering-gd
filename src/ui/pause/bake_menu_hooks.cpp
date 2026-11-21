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
#include <cctype>
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
    if (!self)
        return nullptr;
    return geode::prelude::typeinfo_cast<geode::prelude::CCMenu*>(
        self->getChildByIDRecursive("right-button-menu"));
}

auto EbPauseMenuAttach::menuItemLabel(geode::prelude::CCNode* node) -> std::string {
    auto* item = geode::prelude::typeinfo_cast<CCMenuItemSpriteExtra*>(node);
    if (!item)
        return {};
    if (auto* sprite = geode::prelude::typeinfo_cast<ButtonSprite*>(item->getNormalImage())) {
        if (sprite->m_label) {
            if (auto const* text = sprite->m_label->getString())
                return text;
        }
    }
    // Older installs used plain CCLabelBMFont children.
    if (auto* label = item->getChildByType<cocos2d::CCLabelBMFont>(0)) {
        if (auto const* text = label->getString())
            return text;
    }
    if (auto* normal = item->getNormalImage()) {
        if (auto* label = normal->getChildByType<cocos2d::CCLabelBMFont>(0)) {
            if (auto const* text = label->getString())
                return text;
        }
    }
    return {};
}

static std::string lowerCopy(std::string value) {
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

auto EbPauseMenuAttach::isLegacySideButtonLabel(std::string const& label) -> bool {
    auto const low = lowerCopy(label);
    if (low.empty())
        return false;
    return low == "import cfg" || low == "export cfg" || low == "trajectory" ||
           low == "toggle learn" || low == "reset learn" || low == "toggle debug" ||
           low == "cycle preset" || low == "cycle mode" || low == "import replay" ||
           low == "bake map" || low == "brute bake" || low == "cancel" ||
           low.find("import cfg") != std::string::npos ||
           low.find("export cfg") != std::string::npos ||
           low.find("cycle preset") != std::string::npos ||
           low.find("cycle mode") != std::string::npos ||
           low.find("toggle learn") != std::string::npos ||
           low.find("reset learn") != std::string::npos ||
           low.find("import replay") != std::string::npos ||
           low.find("bake map") != std::string::npos;
}

static bool isLegacySideButtonId(std::string const& id) {
    if (id.empty())
        return false;
    auto const low = lowerCopy(id);
    // Keep the intentional EB settings hub button.
    if (low.find("earlybuffer-settings") != std::string::npos)
        return false;
    if (low.find("earlybuffer-pause-ui") != std::string::npos)
        return false;
    if (low.find("earlybuffer-bake-title") != std::string::npos ||
        low.find("earlybuffer-mode-label") != std::string::npos ||
        low.find("earlybuffer-bake-status") != std::string::npos)
        return false;
    return low.find("earlybuffer-assist") != std::string::npos ||
           low.find("earlybuffer-bake") != std::string::npos ||
           low.find("import-cfg") != std::string::npos ||
           low.find("export-cfg") != std::string::npos ||
           low.find("cycle-preset") != std::string::npos ||
           low.find("cycle-mode") != std::string::npos ||
           low.find("import-replay") != std::string::npos ||
           low.find("toggle-learn") != std::string::npos ||
           low.find("reset-learn") != std::string::npos ||
           low.find("toggle-debug") != std::string::npos ||
           low.find("bake-map") != std::string::npos ||
           low.find("brute-bake") != std::string::npos;
}

auto EbPauseMenuAttach::stripLegacyLabelsFromMenu(geode::prelude::CCMenu* menu) -> void {
    if (!menu)
        return;
    auto* children = menu->getChildren();
    if (!children)
        return;
    bool removed = false;
    for (int index = static_cast<int>(children->count()) - 1; index >= 0; --index) {
        auto* child = static_cast<geode::prelude::CCNode*>(children->objectAtIndex(index));
        if (!child)
            continue;
        std::string const id = child->getID();
        std::string const label = EbPauseMenuAttach::menuItemLabel(child);
        if (EbPauseMenuAttach::isLegacySideButtonLabel(label) || isLegacySideButtonId(id)) {
            menu->removeChild(child, true);
            removed = true;
        }
    }
    if (removed)
        menu->updateLayout();
}

static void stripLegacyMenusRecursive(geode::prelude::CCNode* node) {
    if (!node)
        return;
    auto* children = node->getChildren();
    if (!children)
        return;
    for (int index = static_cast<int>(children->count()) - 1; index >= 0; --index) {
        auto* child = static_cast<geode::prelude::CCNode*>(children->objectAtIndex(index));
        if (!child)
            continue;
        std::string const id = child->getID();
        if (isLegacySideButtonId(id) && !geode::prelude::typeinfo_cast<geode::prelude::CCMenu*>(child)) {
            // Whole leftover side stacks from older installs.
            if (id.find("earlybuffer-assist-menu") != std::string::npos ||
                id.find("earlybuffer-bake-menu") != std::string::npos) {
                child->removeFromParentAndCleanup(true);
                continue;
            }
        }
        if (auto* menu = geode::prelude::typeinfo_cast<geode::prelude::CCMenu*>(child))
            EbPauseMenuAttach::stripLegacyLabelsFromMenu(menu);
        stripLegacyMenusRecursive(child);
    }
}

auto EbPauseMenuAttach::stripLegacySideButtons(PauseLayer* self) -> void {
    if (!self)
        return;
    if (auto* host = EbPauseMenuAttach::hostLayer(self)) {
        for (char const* id : {"earlybuffer-assist-menu"_spr, "earlybuffer-bake-menu"_spr}) {
            if (auto* leftover = host->getChildByID(id))
                leftover->removeFromParentAndCleanup(true);
        }
        stripLegacyMenusRecursive(host);
    }
    stripLegacyMenusRecursive(self);
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
        EbPauseMenuAttach::stripLegacySideButtons(this);
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
    // Other mods / delayed node-ids can re-add side buttons after customSetup.
    EbPauseMenuAttach::stripLegacySideButtons(this);
    EbPauseActionBridge::refreshPauseLabels(this);
    EbPauseActionBridge::refreshOpenSettings();
    EbBakeScheduler::get().tickSlice();
}
