#include "BakeReplayVisualizer.hpp"

#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace {
constexpr float kWidth = 250.f;
constexpr float kHeight = 140.f;
constexpr float kMargin = 12.f;

CCPoint pointFor(
    ShadowSimulator::BakeFrameTrace const& frame,
    float minX, float maxX, float minY, float maxY
) {
    float const xRange = std::max(1.f, maxX - minX);
    float const yRange = std::max(1.f, maxY - minY);
    return {
        kMargin + (frame.x - minX) / xRange * (kWidth - 2.f * kMargin),
        kMargin + (frame.y - minY) / yRange * (kHeight - 2.f * kMargin)
    };
}
}

BakeReplayVisualizer& BakeReplayVisualizer::get() {
    static BakeReplayVisualizer instance;
    return instance;
}

void BakeReplayVisualizer::start(
    PlayLayer* layer,
    std::vector<ShadowSimulator::BakeFrameTrace> frames,
    double tps,
    int firstContactFrame
) {
    stop();
    if (!layer || frames.empty()) return;

    m_node = CCDrawNode::create();
    m_label = CCLabelBMFont::create("", "goldFont.fnt");
    if (!m_node || !m_label) {
        stop();
        return;
    }
    m_node->setID("earlybuffer-bake-replay-preview"_spr);
    m_node->setPosition({ 18.f, 18.f });
    layer->addChild(m_node, 20000);
    layer->addChild(m_label, 20001);
    m_label->setAnchorPoint({ 0.f, 0.f });
    m_label->setPosition({ 22.f, 164.f });
    m_label->setScale(0.45f);
    m_node->retain();
    m_label->retain();

    m_frames = std::move(frames);
    m_tps = std::clamp(tps, 30.0, 4000.0);
    m_cursor = 0.0;
    m_firstContactFrame = firstContactFrame;
    m_active = true;
    rebuild();
}

void BakeReplayVisualizer::stop() {
    m_active = false;
    m_frames.clear();
    if (m_node) {
        m_node->removeFromParentAndCleanup(true);
        m_node->release();
        m_node = nullptr;
    }
    if (m_label) {
        m_label->removeFromParentAndCleanup(true);
        m_label->release();
        m_label = nullptr;
    }
}

void BakeReplayVisualizer::detach(bool) {
    stop();
}

void BakeReplayVisualizer::onPhysicsTick(PlayLayer* layer, float dt) {
    if (!m_active || !layer || !m_node || m_frames.empty()) return;
    m_cursor += std::max(0.f, dt) * m_tps;
    if (m_cursor >= static_cast<double>(m_frames.size() - 1)) {
        m_cursor = static_cast<double>(m_frames.size() - 1);
        m_active = false;
    }
    rebuild();
}

void BakeReplayVisualizer::rebuild() {
    if (!m_node || !m_label || m_frames.empty()) return;
    float minX = m_frames.front().x;
    float maxX = minX;
    float minY = m_frames.front().y;
    float maxY = minY;
    for (auto const& frame : m_frames) {
        minX = std::min(minX, frame.x);
        maxX = std::max(maxX, frame.x);
        minY = std::min(minY, frame.y);
        maxY = std::max(maxY, frame.y);
    }

    m_node->clear();
    CCPoint background[4] = {
        { 0.f, 0.f }, { kWidth, 0.f }, { kWidth, kHeight }, { 0.f, kHeight }
    };
    m_node->drawPolygon(background, 4, { 0.f, 0.f, 0.f, 0.65f }, 0.f, { 0.f, 0.f, 0.f, 0.f });
    for (size_t i = 1; i < m_frames.size(); ++i) {
        auto const a = pointFor(m_frames[i - 1], minX, maxX, minY, maxY);
        auto const b = pointFor(m_frames[i], minX, maxX, minY, maxY);
        auto const color = m_frames[i].dead
            ? ccColor4F{ 1.f, 0.18f, 0.18f, 0.95f }
            : ccColor4F{ 0.12f, 0.88f, 1.f, 0.8f };
        m_node->drawSegment(a, b, 1.2f, color);
    }
    if (m_firstContactFrame >= 0 &&
        m_firstContactFrame < static_cast<int>(m_frames.size())) {
        auto const p = pointFor(m_frames[static_cast<size_t>(m_firstContactFrame)], minX, maxX, minY, maxY);
        m_node->drawDot(p, 4.f, { 1.f, 0.15f, 0.15f, 1.f });
    }
    auto const frameIndex = static_cast<size_t>(m_cursor);
    auto const cursor = pointFor(m_frames[frameIndex], minX, maxX, minY, maxY);
    m_node->drawDot(cursor, 4.f, { 1.f, 0.95f, 0.2f, 1.f });
    m_label->setString(fmt::format(
        "BAKE PLAYBACK  f{}/{}  contact {}",
        m_frames[frameIndex].frame,
        m_frames.back().frame,
        m_firstContactFrame < 0 ? "none" : fmt::format("f{}", m_firstContactFrame)
    ).c_str());
}
