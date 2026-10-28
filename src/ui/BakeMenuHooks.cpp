#include "PauseActionBridge.hpp"
#include "../core/BakeScheduler.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

// Resolve geode.node-ids PauseLayer menus (live under main-layer, not PauseLayer root).
class PauseMenuAttach {
public:
    static CCNode* hostLayer(PauseLayer* self) {
        if (!self) return nullptr;
        if (auto* main = self->getChildByID("main-layer")) return main;
        return self;
    }

    static CCMenu* rightButtonMenu(PauseLayer* self) {
        if (!self) return nullptr;
        return typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("right-button-menu"));
    }
};

class $modify(EbPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        // Clean pause: EB opens Geode settings (category pages). Optional status lines only.
        // Bake / Assist action buttons live under EB → Bake / Assist — no side stacks.
        bool const showStatus = StageConfig::showPauseBake();
        bool const showHub = StageConfig::showPauseHub();
        if (!showStatus && !showHub) return;

        // Already installed (idempotent). Still (re)schedule poll — prior early-return skipped it.
        if (this->getChildByIDRecursive("earlybuffer-pause-ui"_spr)) {
            PauseActionBridge::refreshPauseLabels(this);
            this->schedule(schedule_selector(EbPauseLayer::onBakePoll), 0.15f);
            return;
        }

        auto* host = PauseMenuAttach::hostLayer(this);
        if (!host) return;

        auto win = this->getContentSize();
        auto* marker = CCNode::create();
        marker->setID("earlybuffer-pause-ui"_spr);
        host->addChild(marker);

        // Top-right: opens Geode ModSettingsPopup (Main/Bake/Ingame/Assist/…).
        if (showHub) {
            auto* spr = CircleButtonSprite::create(
                CCLabelBMFont::create("EB", "bigFont.fnt"),
                CircleBaseColor::Cyan,
                CircleBaseSize::Small
            );
            if (!spr) {
                spr = CircleButtonSprite::createWithSpriteFrameName(
                    "GJ_optionsBtn_001.png", 0.6f, CircleBaseColor::Cyan, CircleBaseSize::Small
                );
            }
            auto* btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::openHub();
            });
            btn->setID("earlybuffer-settings-btn"_spr);
            btn->setScale(0.85f);

            if (auto* right = PauseMenuAttach::rightButtonMenu(this)) {
                right->addChild(btn);
                right->updateLayout();
                AssistLog::debug("pause-ui: EB settings on right-button-menu");
            }
            else {
                auto* menu = CCMenu::create();
                menu->setID("earlybuffer-settings-menu"_spr);
                menu->ignoreAnchorPointForPosition(false);
                menu->setAnchorPoint({ 1.f, 1.f });
                menu->setPosition({ win.width - 12.f, win.height - 12.f });
                menu->addChild(btn);
                host->addChild(menu, 1100);
                AssistLog::debug("pause-ui: EB settings fallback top-right menu");
            }
        }

        if (showStatus) {
            auto* title = CCLabelBMFont::create("EarlyBuffer", "goldFont.fnt");
            if (!title) title = CCLabelBMFont::create("EarlyBuffer", "bigFont.fnt");
            title->setID("earlybuffer-bake-title"_spr);
            title->setScale(0.45f);
            title->setAnchorPoint({ 0.f, 1.f });
            title->setPosition({ 14.f, win.height - 52.f });
            host->addChild(title, 999);

            auto* modeLbl = CCLabelBMFont::create("Mode: …", "chatFont.fnt");
            if (!modeLbl) modeLbl = CCLabelBMFont::create("Mode: …", "bigFont.fnt");
            modeLbl->setID("earlybuffer-mode-label"_spr);
            modeLbl->setScale(0.5f);
            modeLbl->setAnchorPoint({ 0.f, 1.f });
            modeLbl->setPosition({ 14.f, win.height - 72.f });
            host->addChild(modeLbl, 999);

            auto* status = CCLabelBMFont::create("…", "chatFont.fnt");
            if (!status) status = CCLabelBMFont::create("…", "bigFont.fnt");
            status->setID("earlybuffer-bake-status"_spr);
            status->setScale(0.45f);
            status->setAnchorPoint({ 0.f, 1.f });
            status->setOpacity(210);
            status->setPosition({ 14.f, win.height - 92.f });
            host->addChild(status, 999);
        }

        PauseActionBridge::refreshPauseLabels(this);
        this->schedule(schedule_selector(EbPauseLayer::onBakePoll), 0.15f);
    }

    void onBakePoll(float) {
        PauseActionBridge::refreshPauseLabels(this);
        PauseActionBridge::refreshOpenSettings();
        BakeScheduler::get().tickSlice();
    }
};
