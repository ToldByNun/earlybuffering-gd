#include "SaveFrameCounter.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

SaveFrameCounter& SaveFrameCounter::get() {
    static SaveFrameCounter inst;
    return inst;
}

void SaveFrameCounter::ensureRoot(PlayLayer* layer) {
    if (!layer) return;
    if (this->m_root && this->m_root->getParent()) return;

    if (this->m_root) {
        this->m_root->release();
        this->m_root = nullptr;
    }

    this->m_root = CCNode::create();
    if (!this->m_root) return;

    this->m_root->setID("earlybuffer-save-counter-root"_spr);
    if (layer->m_objectLayer) {
        layer->m_objectLayer->addChild(this->m_root, 10000);
    }
    else {
        layer->addChild(this->m_root, 10000);
    }
    this->m_root->retain();
}

void SaveFrameCounter::attach(PlayLayer* layer) {
    this->ensureRoot(layer);
}

void SaveFrameCounter::detach(bool layerDying) {
    if (!this->m_root) return;

    if (!layerDying && this->m_root->getParent()) {
        this->m_root->removeFromParent();
    }
    this->m_root->release();
    this->m_root = nullptr;
}

void SaveFrameCounter::clear() {
    if (!this->m_root) return;
    this->m_root->removeAllChildren();
}

void SaveFrameCounter::spawn(PlayLayer* layer, CCPoint worldPos, int delayTicks) {
    (void)layer;
    (void)worldPos;
    (void)delayTicks;
    // Wired in the spawn/visual pass (circle + +N label).
}
