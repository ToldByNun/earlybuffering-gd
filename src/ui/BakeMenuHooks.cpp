#include "PauseActionBridge.hpp"
#include "../core/BakeScheduler.hpp"
#include "../config/StageConfig.hpp"

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

class $modify(EbPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        bool const showBake = StageConfig::showPauseBake();
        bool const showAssist = StageConfig::showPauseAssist();
        if (!showBake && !showAssist) return;

        if (this->getChildByID("earlybuffer-bake-menu"_spr)
            || this->getChildByID("earlybuffer-assist-menu"_spr)) {
            PauseActionBridge::refreshPauseLabels(this);
            return;
        }

        auto win = this->getContentSize();

        if (showBake) {
            auto* title = CCLabelBMFont::create("EarlyBuffer bake", "goldFont.fnt");
            if (!title) title = CCLabelBMFont::create("EarlyBuffer bake", "bigFont.fnt");
            title->setID("earlybuffer-bake-title"_spr);
            title->setScale(0.45f);
            title->setAnchorPoint({ 0.f, 1.f });
            title->setPosition({ 14.f, win.height - 52.f });
            this->addChild(title, 999);

            auto* modeLbl = CCLabelBMFont::create("Mode: …", "chatFont.fnt");
            if (!modeLbl) modeLbl = CCLabelBMFont::create("Mode: …", "bigFont.fnt");
            modeLbl->setID("earlybuffer-mode-label"_spr);
            modeLbl->setScale(0.5f);
            modeLbl->setAnchorPoint({ 0.f, 1.f });
            modeLbl->setPosition({ 14.f, win.height - 72.f });
            this->addChild(modeLbl, 999);

            auto* status = CCLabelBMFont::create("…", "chatFont.fnt");
            if (!status) status = CCLabelBMFont::create("…", "bigFont.fnt");
            status->setID("earlybuffer-bake-status"_spr);
            status->setScale(0.45f);
            status->setAnchorPoint({ 0.f, 1.f });
            status->setOpacity(210);
            status->setPosition({ 14.f, win.height - 92.f });
            this->addChild(status, 999);

            auto* col = CCMenu::create();
            col->setID("earlybuffer-bake-menu"_spr);
            col->setContentSize({ 160.f, 220.f });
            col->ignoreAnchorPointForPosition(false);
            col->setAnchorPoint({ 0.f, 1.f });
            col->setPosition({ 14.f, win.height - 130.f });

            col->addChild(PauseMenuButtonFactory::makeTextBtn("Cycle Mode", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::cycleAssistMode();
            }));
            col->addChild(PauseMenuButtonFactory::makeTextBtn("Import Replay", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::importReplay();
            }));
            col->addChild(PauseMenuButtonFactory::makeTextBtn("Bake Map", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::startBake(false);
            }));
            col->addChild(PauseMenuButtonFactory::makeTextBtn("Brute Bake", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::startBake(true);
            }));
            col->addChild(PauseMenuButtonFactory::makeTextBtn("Cancel", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::cancelBake();
            }));

            col->setLayout(
                ColumnLayout::create()
                    ->setGap(4.f)
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::Start)
                    ->setAutoScale(false)
            );
            col->updateLayout();
            this->addChild(col, 1000);
        }

        if (showAssist) {
            auto* acol = CCMenu::create();
            acol->setID("earlybuffer-assist-menu"_spr);
            acol->setContentSize({ 160.f, 280.f });
            acol->ignoreAnchorPointForPosition(false);
            acol->setAnchorPoint({ 1.f, 1.f });
            acol->setPosition({ win.width - 14.f, win.height - 52.f });

            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Cycle Preset", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::cyclePreset();
            }));
            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Toggle Debug", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::toggleDebug();
            }));
            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Reset Learn", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::resetLearning();
            }));
            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Toggle Learn", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::toggleAutoTune();
            }));
            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Trajectory", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::toggleTrajectory();
            }));
            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Export Cfg", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::exportConfig();
            }));
            acol->addChild(PauseMenuButtonFactory::makeTextBtn("Import Cfg", 140.f, [](CCMenuItemSpriteExtra*) {
                PauseActionBridge::importConfig();
            }));

            acol->setLayout(
                ColumnLayout::create()
                    ->setGap(4.f)
                    ->setAxisAlignment(AxisAlignment::Start)
                    ->setCrossAxisAlignment(AxisAlignment::End)
                    ->setAutoScale(false)
            );
            acol->updateLayout();
            this->addChild(acol, 1000);
        }

        PauseActionBridge::refreshPauseLabels(this);
        this->schedule(schedule_selector(EbPauseLayer::onBakePoll), 0.15f);
    }

    void onBakePoll(float) {
        PauseActionBridge::refreshPauseLabels(this);
        BakeScheduler::get().tickSlice();
    }
};
