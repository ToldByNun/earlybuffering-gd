#include "status_overlay.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/platform/platform_limits.hpp"

cocos2d::CCLabelBMFont* EbStatusOverlay::s_label = nullptr;

std::string EbStatusOverlayImplementation::s_lastText;
bool EbStatusOverlayImplementation::s_lastVisible = false;

void EbStatusOverlay::attach(PlayLayer* playLayer) {
    detach();
    if (!playLayer)
        return;

    s_label = geode::prelude::CCLabelBMFont::create("EarlyBuffer", "chatFont.fnt");
    if (!s_label) {
        s_label = geode::prelude::CCLabelBMFont::create("EarlyBuffer", "bigFont.fnt");
    }
    if (!s_label)
        return;

    s_label->setScale(0.42f);
    s_label->setAnchorPoint({0.f, 1.f});
    s_label->setOpacity(220);
    s_label->setAlignment(geode::prelude::kCCTextAlignmentLeft);
    s_label->setID("earlybuffer-status-overlay"_spr);

    auto const winSize = geode::prelude::CCDirector::sharedDirector()->getWinSize();
    auto& limits = EbPlatformLimits::get();
    float const left = limits.overlayLeftInset();
    float const top = limits.overlayTopInset();
    s_label->setPosition({left, winSize.height - top});

    EbStatusOverlayImplementation::s_lastText = "EarlyBuffer";
    EbStatusOverlayImplementation::s_lastVisible = EbStageConfig::showOverlay();
    s_label->setVisible(EbStatusOverlayImplementation::s_lastVisible);

    playLayer->addChild(s_label, 1000);
    s_label->retain();
}

void EbStatusOverlay::detach(bool layerDying) {
    if (!s_label) {
        EbStatusOverlayImplementation::s_lastText.clear();
        return;
    }
    if (layerDying) {
        s_label->release();
        s_label = nullptr;
        EbStatusOverlayImplementation::s_lastText.clear();
        return;
    }
    if (s_label->getParent()) {
        s_label->removeFromParent();
    }
    s_label->release();
    s_label = nullptr;
    EbStatusOverlayImplementation::s_lastText.clear();
}

void EbStatusOverlay::update(std::string const& text) {
    if (!s_label)
        return;
    bool const vis = EbStageConfig::showOverlay();
    if (vis != EbStatusOverlayImplementation::s_lastVisible) {
        EbStatusOverlayImplementation::s_lastVisible = vis;
        s_label->setVisible(vis);
    }
    if (vis && text != EbStatusOverlayImplementation::s_lastText) {
        EbStatusOverlayImplementation::s_lastText = text;
        s_label->setString(text.c_str());
    }
}

void EbStatusOverlay::setVisible(bool visible) {
    if (!s_label)
        return;
    bool const vis = visible && EbStageConfig::showOverlay();
    if (vis != EbStatusOverlayImplementation::s_lastVisible) {
        EbStatusOverlayImplementation::s_lastVisible = vis;
        s_label->setVisible(vis);
    }
}
