#include "SaveFrameCounter.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <fmt/format.h>

using namespace geode::prelude;

SaveFrameCounter& SaveFrameCounter::get() {
    static SaveFrameCounter inst;
    return inst;
}

int SaveFrameCounter::bucketFrames(int delayTicks) {
    if (delayTicks <= 0) return 1;
    return delayTicks >= 8 ? 8 : delayTicks;
}

ccColor3B SaveFrameCounter::colorForBucket(int bucket) {
    switch (bucket) {
        case 1: return { 0xff, 0x61, 0x4e };
        case 2: return { 0xff, 0xb4, 0x4a };
        case 3: return { 0xff, 0xf6, 0x72 };
        case 4: return { 0xfe, 0xfe, 0xfe };
        case 5: return { 0x83, 0xed, 0x8b };
        case 6: return { 0x70, 0xc3, 0xff };
        case 7: return { 0x77, 0x7e, 0xff };
        case 8:
        default: return { 0xc0, 0x69, 0xfa };
    }
}

ccColor4F SaveFrameCounter::to4F(ccColor3B c, float a) {
    return {
        static_cast<float>(c.r) / 255.f,
        static_cast<float>(c.g) / 255.f,
        static_cast<float>(c.b) / 255.f,
        a
    };
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

void SaveFrameCounter::dropOldestIfOverCap() {
    if (!this->m_root) return;
    auto* kids = this->m_root->getChildren();
    if (!kids) return;

    while (static_cast<int>(kids->count()) >= kMaxMarkers) {
        auto* oldest = static_cast<CCNode*>(kids->objectAtIndex(0));
        if (!oldest) break;
        oldest->removeFromParent();
        kids = this->m_root->getChildren();
        if (!kids) break;
    }
}

CCNode* SaveFrameCounter::makeMarker(CCPoint worldPos, int delayTicks) {
    int const bucket = bucketFrames(delayTicks);
    auto const rgb = colorForBucket(bucket);
    int const shown = std::min(std::max(delayTicks, 1), 99);

    auto* marker = CCNode::create();
    if (!marker) return nullptr;
    marker->setPosition(worldPos);
    marker->setID("earlybuffer-save-marker"_spr);

    auto* draw = CCDrawNode::create();
    if (!draw) return nullptr;

    draw->setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
    // Hollow ring: transparent fill + solid border (matches click-indicator look).
    draw->drawCircle(
        CCPointZero,
        kRingRadius,
        to4F(rgb, 0.f),
        kRingBorder,
        to4F(rgb, 0.95f),
        kRingSegments
    );
    // Soft inner tint so the hole reads on dark/busy backgrounds.
    draw->drawCircle(
        CCPointZero,
        kRingRadius - kRingBorder,
        to4F(rgb, 0.12f),
        0.f,
        to4F(rgb, 0.f),
        kRingSegments
    );
    marker->addChild(draw);

    auto* label = CCLabelBMFont::create(
        fmt::format("+{}", shown).c_str(), "bigFont.fnt"
    );
    if (!label) {
        label = CCLabelBMFont::create(
            fmt::format("+{}", shown).c_str(), "chatFont.fnt"
        );
    }
    if (label) {
        label->setScale(kLabelScale);
        label->setColor(rgb);
        label->setAnchorPoint({ 1.f, 0.5f });
        label->setPosition({ -(kRingRadius + kLabelGap), 0.f });
        label->setID("earlybuffer-save-marker-label"_spr);
        marker->addChild(label);
    }

    return marker;
}

void SaveFrameCounter::spawn(PlayLayer* layer, CCPoint worldPos, int delayTicks) {
    if (!layer) return;
    if (delayTicks <= 0) return;

    this->ensureRoot(layer);
    if (!this->m_root) return;

    this->dropOldestIfOverCap();

    auto* marker = makeMarker(worldPos, delayTicks);
    if (!marker) return;
    this->m_root->addChild(marker);
}
