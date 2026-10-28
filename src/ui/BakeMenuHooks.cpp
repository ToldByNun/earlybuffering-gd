#include "PauseActionBridge.hpp"
#include "../core/BakeScheduler.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

class PauseMenuButtonFactory {
public:
    template <class Cb>
    static CCMenuItemSpriteExtra* makeTextBtn(char const* text, float width, Cb&& cb) {
        auto* btn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create(
                text, static_cast<int>(width), true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f
            ),
            std::forward<Cb>(cb)
        );
        btn->setScale(0.72f);
        return btn;
    }
};

// Resolve geode.node-ids PauseLayer menus (live under main-layer, not PauseLayer root).
class PauseMenuAttach {
public:
    static CCNode* hostLayer(PauseLayer* self) {
        if (!self) return nullptr;
        if (auto* main = self->getChildByID("main-layer")) return main;
        return self;
    }

    static CCMenu* leftButtonMenu(PauseLayer* self) {
        if (!self) return nullptr;
        return typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("left-button-menu"));
    }

    static CCMenu* rightButtonMenu(PauseLayer* self) {
        if (!self) return nullptr;
        return typeinfo_cast<CCMenu*>(self->getChildByIDRecursive("right-button-menu"));
    }

    // Widen NodeIDs side menus (default ~40px) so text buttons receive hits.
    static void prepareSideMenu(CCMenu* menu, float width) {
        if (!menu) return;
        auto sz = menu->getContentSize();
        if (sz.width < width) {
            menu->setContentSize({ width, sz.height });
        }
    }
};

class $modify(EbPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        bool const showBake = StageConfig::showPauseBake();
        bool const showAssist = StageConfig::showPauseAssist();
        bool const showHub = StageConfig::showPauseHub();
        if (!showBake && !showAssist && !showHub) return;

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

        // Top-right: opens Geode ModSettingsPopup for EarlyBuffer (search / titles / pages).
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
                PauseActionBridge::openModSettings();
            });
            btn->setID("earlybuffer-settings-btn"_spr);
            btn->setScale(0.85f);

            bool placedOnRight = false;
            if (auto* right = PauseMenuAttach::rightButtonMenu(this)) {
                // Prefer NodeIDs right column (same touch path as options gear).
                right->addChild(btn);
                right->updateLayout();
                placedOnRight = true;
                AssistLog::debug("pause-ui: EB settings on right-button-menu");
            }
            if (!placedOnRight) {
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

        if (showBake) {
            auto* title = CCLabelBMFont::create("EarlyBuffer bake", "goldFont.fnt");
            if (!title) title = CCLabelBMFont::create("EarlyBuffer bake", "bigFont.fnt");
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

            auto addBakeButtons = [&](CCMenu* menu) {
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Cycle Mode", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::cycleAssistMode();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Import Replay", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::importReplay();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Bake Map", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::startBake(false);
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Brute Bake", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::startBake(true);
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Cancel", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::cancelBake();
                    }
                ));
            };

            if (auto* left = PauseMenuAttach::leftButtonMenu(this)) {
                PauseMenuAttach::prepareSideMenu(left, 160.f);
                addBakeButtons(left);
                left->updateLayout();
                AssistLog::debug("pause-ui: bake buttons on left-button-menu");
            }
            else {
                // Fallback when node-ids menus are missing: menu under main-layer (touch path).
                AssistLog::debug("pause-ui: left-button-menu missing — fallback CCMenu on main-layer");
                auto* col = CCMenu::create();
                col->setID("earlybuffer-bake-menu"_spr);
                col->setContentSize({ 160.f, 220.f });
                col->ignoreAnchorPointForPosition(false);
                col->setAnchorPoint({ 0.f, 1.f });
                col->setPosition({ 14.f, win.height - 130.f });
                addBakeButtons(col);
                col->setLayout(
                    ColumnLayout::create()
                        ->setGap(4.f)
                        ->setAxisAlignment(AxisAlignment::Start)
                        ->setCrossAxisAlignment(AxisAlignment::Start)
                        ->setAutoScale(false)
                );
                col->updateLayout();
                host->addChild(col, 1000);
            }
        }

        if (showAssist) {
            auto addAssistButtons = [&](CCMenu* menu) {
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Cycle Preset", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::cyclePreset();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Toggle Debug", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::toggleDebug();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Reset Learn", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::resetLearning();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Toggle Learn", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::toggleAutoTune();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Trajectory", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::toggleTrajectory();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Export Cfg", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::exportConfig();
                    }
                ));
                menu->addChild(PauseMenuButtonFactory::makeTextBtn(
                    "Import Cfg", 140.f, [](CCMenuItemSpriteExtra*) {
                        PauseActionBridge::importConfig();
                    }
                ));
            };

            if (auto* right = PauseMenuAttach::rightButtonMenu(this)) {
                PauseMenuAttach::prepareSideMenu(right, 160.f);
                addAssistButtons(right);
                right->updateLayout();
                AssistLog::debug("pause-ui: assist buttons on right-button-menu");
            }
            else {
                AssistLog::debug("pause-ui: right-button-menu missing — fallback CCMenu on main-layer");
                auto* acol = CCMenu::create();
                acol->setID("earlybuffer-assist-menu"_spr);
                acol->setContentSize({ 160.f, 280.f });
                acol->ignoreAnchorPointForPosition(false);
                acol->setAnchorPoint({ 1.f, 1.f });
                acol->setPosition({ win.width - 14.f, win.height - 52.f });
                addAssistButtons(acol);
                acol->setLayout(
                    ColumnLayout::create()
                        ->setGap(4.f)
                        ->setAxisAlignment(AxisAlignment::Start)
                        ->setCrossAxisAlignment(AxisAlignment::End)
                        ->setAutoScale(false)
                );
                acol->updateLayout();
                host->addChild(acol, 1000);
            }
        }

        PauseActionBridge::refreshPauseLabels(this);
        this->schedule(schedule_selector(EbPauseLayer::onBakePoll), 0.15f);
    }

    void onBakePoll(float) {
        PauseActionBridge::refreshPauseLabels(this);
        BakeScheduler::get().tickSlice();
    }
};
