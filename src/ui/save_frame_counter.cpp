#include "save_frame_counter.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <fmt/format.h>
#include <string>

SaveFrameCounter& SaveFrameCounter::get() {
    static SaveFrameCounter inst;
    return inst;
}

int SaveFrameCounter::bucketFrames(int delayTicks) {
    if (!(delayTicks <= 0)) {
        return delayTicks >= 8 ? 8 : delayTicks;
    }
    return 1;
}

geode::prelude::ccColor3B SaveFrameCounter::colorForBucket(int bucket) {
    switch (bucket) {
    case 1:
        return {0xff, 0x61, 0x4e};
    case 2:
        return {0xff, 0xb4, 0x4a};
    case 3:
        return {0xff, 0xf6, 0x72};
    case 4:
        return {0xfe, 0xfe, 0xfe};
    case 5:
        return {0x83, 0xed, 0x8b};
    case 6:
        return {0x70, 0xc3, 0xff};
    case 7:
        return {0x77, 0x7e, 0xff};
    case 8:
    default:
        return {0xc0, 0x69, 0xfa};
    }
}

geode::prelude::ccColor4F SaveFrameCounter::to4F(geode::prelude::ccColor3B candidate,
                                                 float firstValue) {
    return {static_cast<float>(candidate.r) / 255.f, static_cast<float>(candidate.g) / 255.f,
            static_cast<float>(candidate.b) / 255.f, firstValue};
}

void SaveFrameCounter::ensureRoot(PlayLayer* layer) {
    if (!layer)
        return;
    if (this->m_root && this->m_root->getParent())
        return;

    if (this->m_root) {
        this->m_root->release();
        this->m_root = nullptr;
    }

    this->m_root = geode::prelude::CCNode::create();
    if (!this->m_root)
        return;

    this->m_root->setID("earlybuffer-save-counter-root"_spr);
    if (layer->m_objectLayer) {
        layer->m_objectLayer->addChild(this->m_root, 10000);
    } else {
        layer->addChild(this->m_root, 10000);
    }
    this->m_root->retain();
}

void SaveFrameCounter::attach(PlayLayer* layer) {
    this->ensureRoot(layer);
}

void SaveFrameCounter::detach(bool layerDying) {
    if (!this->m_root)
        return;
    if (layerDying) {
        // Drop OUR retain only; stay parented so PlayLayer teardown frees once.
        // Do not removeFromParent here — setNextScene is already destroying the tree.
        this->m_root->release();
        this->m_root = nullptr;
        return;
    }
    // Live quit: strip markers then destroy root while objectLayer is alive.
    this->m_root->removeAllChildrenWithCleanup(true);
    if (this->m_root->getParent()) {
        this->m_root->removeFromParentAndCleanup(true);
    }
    this->m_root->release();
    this->m_root = nullptr;
}

void SaveFrameCounter::clear() {
    if (!this->m_root)
        return;
    this->m_root->removeAllChildren();
}

void SaveFrameCounter::dropOldestIfOverCap() {
    if (!this->m_root)
        return;
    auto* kids = this->m_root->getChildren();
    if (!kids)
        return;

    while (static_cast<int>(kids->count()) >= kMaxMarkers) {
        auto* oldest = static_cast<geode::prelude::CCNode*>(kids->objectAtIndex(0));
        if (!oldest)
            break;
        oldest->removeFromParent();
        kids = this->m_root->getChildren();
        if (!kids)
            break;
    }
}

void SaveFrameCounter::addOutlinedLabel(geode::prelude::CCNode* parent, char const* text,
                                        geode::prelude::ccColor3B color, float x, float y) {
    if (!parent || !text)
        return;

    char const* fontName = "bigFont.fnt";
    auto* label = geode::prelude::CCLabelBMFont::create(text, fontName);
    if (!label) {
        fontName = "chatFont.fnt";
        label = geode::prelude::CCLabelBMFont::create(text, fontName);
        if (!label)
            return;
    }

    geode::prelude::ccColor3B const outline = {0, 0, 0};
    float const outputStream = kLabelOutlinePx;

    // 8-direction thick black outline in parent space (independent of label scale).
    float const objectX[8] = {-outputStream, outputStream,  0.f,          0.f,
                              -outputStream, -outputStream, outputStream, outputStream};
    float const objectY[8] = {0.f,           0.f,          -outputStream, outputStream,
                              -outputStream, outputStream, -outputStream, outputStream};
    for (int index = 0; index < 8; ++index) {
        auto* shadow = geode::prelude::CCLabelBMFont::create(text, fontName);
        if (!shadow)
            continue;
        shadow->setScale(kLabelScale);
        shadow->setColor(outline);
        shadow->setOpacity(255);
        shadow->setAnchorPoint({1.f, 0.5f});
        shadow->setPosition({x + objectX[index], y + objectY[index]});
        parent->addChild(shadow, 0);
    }

    label->setScale(kLabelScale);
    label->setColor(color);
    label->setAnchorPoint({1.f, 0.5f});
    label->setPosition({x, y});
    label->setID("earlybuffer-save-marker-label"_spr);
    parent->addChild(label, 1);
}

geode::prelude::CCNode* SaveFrameCounter::makeMarker(geode::prelude::CCPoint worldPos,
                                                     int delayTicks) {
    int const bucket = bucketFrames(delayTicks);
    auto const rgb = colorForBucket(bucket);
    // 1..7 → "+Nt"; 8 or more → "8+t" (physics ticks, not 60fps frames).
    std::string const text =
        (delayTicks >= 8) ? std::string("8+t") : fmt::format("+{}t", std::max(delayTicks, 1));

    auto* marker = geode::prelude::CCNode::create();
    if (!marker)
        return nullptr;
    marker->setPosition(worldPos);
    marker->setID("earlybuffer-save-marker"_spr);

    auto* draw = geode::prelude::CCDrawNode::create();
    if (!draw)
        return nullptr;

    draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});

    geode::prelude::ccColor3B const black = {0, 0, 0};
    float const outerR = kRingRadius + kOutlinePad;
    float const innerR = std::max(1.f, kRingRadius - kRingBorder);

    // Outer black rim.
    draw->drawCircle(geode::prelude::CCPointZero, outerR, to4F(black, 0.f), kOutlineBorder,
                     to4F(black, 1.f), kRingSegments);
    // Colored ring body.
    draw->drawCircle(geode::prelude::CCPointZero, kRingRadius, to4F(rgb, 0.f), kRingBorder,
                     to4F(rgb, 0.95f), kRingSegments);
    // Inner black rim (inside the colored stroke).
    draw->drawCircle(geode::prelude::CCPointZero, innerR, to4F(black, 0.f), kOutlineBorder,
                     to4F(black, 1.f), kRingSegments);
    // Soft inner tint so the hole reads on dark/busy backgrounds.
    draw->drawCircle(geode::prelude::CCPointZero, std::max(0.5f, innerR - kOutlineBorder),
                     to4F(rgb, 0.12f), 0.f, to4F(rgb, 0.f), kRingSegments);
    marker->addChild(draw);

    addOutlinedLabel(marker, text.c_str(), rgb, -(kRingRadius + kLabelGap), 0.f);

    return marker;
}

void SaveFrameCounter::spawn(PlayLayer* layer, geode::prelude::CCPoint worldPos, int delayTicks) {
    if (!layer)
        return;
    if (delayTicks <= 0)
        return;

    this->ensureRoot(layer);
    if (!this->m_root)
        return;

    this->dropOldestIfOverCap();

    auto* marker = makeMarker(worldPos, delayTicks);
    if (!marker)
        return;
    this->m_root->addChild(marker);
}
