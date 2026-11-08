#include "status_overlay.hpp"
#include "../config/stage_config.hpp"
#include "../core/platform_limits.hpp"

cocos2d::CCLabelBMFont* StatusOverlay::s_label = nullptr;

std::string StatusOverlayImplementation::s_lastText;
bool StatusOverlayImplementation::s_lastVisible = false;

void StatusOverlay::attach(PlayLayer* playLayer) {
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
    auto& limits = PlatformLimits::get();
    float const left = limits.overlayLeftInset();
    float const top = limits.overlayTopInset();
    s_label->setPosition({left, winSize.height - top});

    StatusOverlayImplementation::s_lastText = "EarlyBuffer";
    StatusOverlayImplementation::s_lastVisible = StageConfig::showOverlay();
    s_label->setVisible(StatusOverlayImplementation::s_lastVisible);

    playLayer->addChild(s_label, 1000);
    s_label->retain();
}

void StatusOverlay::detach(bool layerDying) {
    if (!s_label) {
        StatusOverlayImplementation::s_lastText.clear();
        return;
    }
    if (layerDying) {
        s_label->release();
        s_label = nullptr;
        StatusOverlayImplementation::s_lastText.clear();
        return;
    }
    if (s_label->getParent()) {
        s_label->removeFromParent();
    }
    s_label->release();
    s_label = nullptr;
    StatusOverlayImplementation::s_lastText.clear();
}

void StatusOverlay::update(std::string const& text) {
    if (!s_label)
        return;
    bool const vis = StageConfig::showOverlay();
    if (vis != StatusOverlayImplementation::s_lastVisible) {
        StatusOverlayImplementation::s_lastVisible = vis;
        s_label->setVisible(vis);
    }
    if (vis && text != StatusOverlayImplementation::s_lastText) {
        StatusOverlayImplementation::s_lastText = text;
        s_label->setString(text.c_str());
    }
}

void StatusOverlay::setVisible(bool visible) {
    if (!s_label)
        return;
    bool const vis = visible && StageConfig::showOverlay();
    if (vis != StatusOverlayImplementation::s_lastVisible) {
        StatusOverlayImplementation::s_lastVisible = vis;
        s_label->setVisible(vis);
    }
}
