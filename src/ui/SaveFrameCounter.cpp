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
    (void)layerDying;
    // Unlink before release — same retain trap as shadow ghosts on quit.
    if (this->m_root->getParent()) {
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

void SaveFrameCounter::addOutlinedLabel(
    CCNode* parent,
    char const* text,
    ccColor3B color,
    float x,
    float y
) {
    if (!parent || !text) return;

    char const* fontName = "bigFont.fnt";
    auto* label = CCLabelBMFont::create(text, fontName);
    if (!label) {
        fontName = "chatFont.fnt";
        label = CCLabelBMFont::create(text, fontName);
        if (!label) return;
    }

    ccColor3B const outline = { 0, 0, 0 };
    float const o = kLabelOutlinePx;

    // 8-direction thick black outline in parent space (independent of label scale).
    float const ox[8] = { -o, o, 0.f, 0.f, -o, -o, o, o };
    float const oy[8] = { 0.f, 0.f, -o, o, -o, o, -o, o };
    for (int i = 0; i < 8; ++i) {
        auto* shadow = CCLabelBMFont::create(text, fontName);
        if (!shadow) continue;
        shadow->setScale(kLabelScale);
        shadow->setColor(outline);
        shadow->setOpacity(255);
        shadow->setAnchorPoint({ 1.f, 0.5f });
        shadow->setPosition({ x + ox[i], y + oy[i] });
        parent->addChild(shadow, 0);
    }

    label->setScale(kLabelScale);
    label->setColor(color);
    label->setAnchorPoint({ 1.f, 0.5f });
    label->setPosition({ x, y });
    label->setID("earlybuffer-save-marker-label"_spr);
    parent->addChild(label, 1);
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

    ccColor3B const black = { 0, 0, 0 };
    float const outerR = kRingRadius + kOutlinePad;
    float const innerR = std::max(1.f, kRingRadius - kRingBorder);

    // Outer black rim.
    draw->drawCircle(
        CCPointZero,
        outerR,
        to4F(black, 0.f),
        kOutlineBorder,
        to4F(black, 1.f),
        kRingSegments
    );
    // Colored ring body.
    draw->drawCircle(
        CCPointZero,
        kRingRadius,
        to4F(rgb, 0.f),
        kRingBorder,
        to4F(rgb, 0.95f),
        kRingSegments
    );
    // Inner black rim (inside the colored stroke).
    draw->drawCircle(
        CCPointZero,
        innerR,
        to4F(black, 0.f),
        kOutlineBorder,
        to4F(black, 1.f),
        kRingSegments
    );
    // Soft inner tint so the hole reads on dark/busy backgrounds.
    draw->drawCircle(
        CCPointZero,
        std::max(0.5f, innerR - kOutlineBorder),
        to4F(rgb, 0.12f),
        0.f,
        to4F(rgb, 0.f),
        kRingSegments
    );
    marker->addChild(draw);

    auto const text = fmt::format("+{}", shown);
    addOutlinedLabel(
        marker,
        text.c_str(),
        rgb,
        -(kRingRadius + kLabelGap),
        0.f
    );

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
