#include "StatusOverlay.hpp"
#include "../config/StageConfig.hpp"

using namespace geode::prelude;


cocos2d::CCLabelBMFont* StatusOverlay::s_label = nullptr;

std::string g_lastText;
bool g_lastVisible = false;


void StatusOverlay::attach(PlayLayer* playLayer) {
    detach();
    if (!playLayer) return;

    s_label = CCLabelBMFont::create("frirecracker", "chatFont.fnt");
    if (!s_label) {
        s_label = CCLabelBMFont::create("frirecracker", "bigFont.fnt");
    }
    if (!s_label) return;

    s_label->setScale(0.42f);
    s_label->setAnchorPoint({ 0.f, 1.f });
    s_label->setOpacity(220);
    s_label->setAlignment(kCCTextAlignmentLeft);
    s_label->setID("frirecracker-status-overlay"_spr);

    auto const winSize = CCDirector::sharedDirector()->getWinSize();
    s_label->setPosition({ 10.f, winSize.height - 10.f });

    g_lastText = "frirecracker";
    g_lastVisible = StageConfig::showOverlay();
    s_label->setVisible(g_lastVisible);

    playLayer->addChild(s_label, 1000);
    s_label->retain();
}

void StatusOverlay::detach(bool layerDying) {
    if (!s_label) {
        g_lastText.clear();
        return;
    }
    if (!layerDying && s_label->getParent()) {
        s_label->removeFromParent();
    }
    s_label->release();
    s_label = nullptr;
    g_lastText.clear();
}

void StatusOverlay::update(std::string const& text) {
    if (!s_label) return;
    bool const vis = StageConfig::showOverlay();
    if (vis != g_lastVisible) {
        g_lastVisible = vis;
        s_label->setVisible(vis);
    }
    if (vis && text != g_lastText) {
        g_lastText = text;
        s_label->setString(text.c_str());
    }
}

void StatusOverlay::setVisible(bool visible) {
    if (!s_label) return;
    bool const vis = visible && StageConfig::showOverlay();
    if (vis != g_lastVisible) {
        g_lastVisible = vis;
        s_label->setVisible(vis);
    }
}


