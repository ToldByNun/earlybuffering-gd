#include "../../app/class_manager.hpp"
#include "straightfly_visualizer.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../debug/perf_trace.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

geode::prelude::ccColor4F EbStraightflyVisualizerImplementation::lineColor(float alpha) {
    return {0.20f, 0.85f, 1.f, alpha};
}

geode::prelude::ccColor4F EbStraightflyVisualizerImplementation::bandColor(float alpha) {
    return {0.15f, 0.70f, 0.95f, alpha};
}

geode::prelude::ccColor4F EbStraightflyVisualizerImplementation::anchorColor(float alpha) {
    return {1.f, 1.f, 1.f, alpha};
}

void EbStraightflyVisualizerImplementation::axis(bool sideways, bool reversed, double degrees,
                                                 double& axisX, double& axisY) {
    double const radians = degrees * std::numbers::pi / 180.;
    double const forward = reversed ? -1. : 1.;
    axisX = sideways ? std::sin(radians) : forward * std::cos(radians);
    axisY = sideways ? forward * std::cos(radians) : std::sin(radians);
}

EbStraightflyVisualizer& EbStraightflyVisualizer::get() {
    return EbClassManager::get().service<EbStraightflyVisualizer>();
}

void EbStraightflyVisualizer::setEnabled(bool on) {
    m_enabled = on;
    if (!on) {
        m_ticksSinceRebuild = 0;
        clear();
    }
}

std::string EbStraightflyVisualizer::statusHint() const {
    if (!m_enabled)
        return {};
    if (m_drawnLines <= 0)
        return "sf-viz idle";
    return fmt::format("sf-viz {}", m_drawnLines);
}

void EbStraightflyVisualizer::attach(PlayLayer* layer) {
    ensureNode(layer);
    m_ticksSinceRebuild = 999;
}

void EbStraightflyVisualizer::detach(bool layerDying) {
    m_enabled = false;
    m_ticksSinceRebuild = 0;
    m_drawnLines = 0;
    if (!m_node)
        return;
    if (layerDying) {
        m_node->release();
        m_node = nullptr;
        return;
    }
    if (m_node->getParent())
        m_node->removeFromParent();
    m_node->release();
    m_node = nullptr;
}

void EbStraightflyVisualizer::clear() {
    if (m_node && m_node->getParent())
        m_node->clear();
    m_drawnLines = 0;
}

void EbStraightflyVisualizer::ensureNode(PlayLayer* layer) {
    if (!layer)
        return;
    if (m_node && m_node->getParent())
        return;
    if (m_node) {
        m_node->release();
        m_node = nullptr;
    }
    m_node = geode::prelude::CCDrawNode::create();
    if (!m_node)
        return;
    m_node->setID("earlybuffer-straightfly"_spr);
    m_node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    // Above trajectory (9999), below bake windows (10000).
    if (layer->m_objectLayer)
        layer->m_objectLayer->addChild(m_node, 9995);
    else
        layer->addChild(m_node, 9995);
    m_node->retain();
}

void EbStraightflyVisualizer::onPhysicsTick(PlayLayer* layer, float) {
    if (!m_enabled || !layer || !m_node)
        return;
    if (EbShadowSimulator::get().isSimulating())
        return;
    if (!layer->m_objectLayer)
        return;
    if (!m_node->getParent()) {
        // Retry/respawn can rebuild objectLayer — reattach instead of disabling forever.
        ensureNode(layer);
        if (!m_node || !m_node->getParent())
            return;
    }
    ++m_ticksSinceRebuild;
    if (m_ticksSinceRebuild < kRebuildEveryTicks)
        return;
    m_ticksSinceRebuild = 0;
    rebuild(layer);
}

void EbStraightflyVisualizer::rebuild(PlayLayer* layer) {
    EbPerfTrace::Scope const scope("straightflyRebuild");
    if (!m_node)
        return;
    m_node->clear();
    m_drawnLines = 0;
    PlayerObject* players[2] = {layer->m_player1, layer->m_player2};
    for (int slot = 0; slot < 2; ++slot) {
        auto* player = players[slot];
        if (!player || player->m_isDead)
            continue;
        auto const view = EbAssistController::get().straightflyView(slot);
        if (!view.valid)
            continue;
        double axisX = 0.;
        double axisY = 0.;
        EbStraightflyVisualizerImplementation::axis(view.sideways, view.reversed, view.degrees,
                                                    axisX, axisY);
        float const alpha = static_cast<float>(std::clamp(view.confidence, 0.25, 1.));
        geode::prelude::CCPoint const anchor{static_cast<float>(view.x),
                                             static_cast<float>(view.y)};
        geode::prelude::CCPoint const ahead{
            static_cast<float>(view.x + axisX * kForwardLength),
            static_cast<float>(view.y + axisY * kForwardLength)};
        geode::prelude::CCPoint const behind{
            static_cast<float>(view.x - axisX * kBackLength),
            static_cast<float>(view.y - axisY * kBackLength)};
        // Outer strips = body half-width × room-of-error (corridorWidth).
        double const half =
            std::max(4., view.bodySize * 0.5 * std::max(.25, view.corridorWidth));
        double const nx = -axisY;
        double const ny = axisX;
        geode::prelude::CCPoint early[2] = {
            {static_cast<float>(view.x + nx * half - axisX * kBackLength),
             static_cast<float>(view.y + ny * half - axisY * kBackLength)},
            {static_cast<float>(view.x + nx * half + axisX * kForwardLength),
             static_cast<float>(view.y + ny * half + axisY * kForwardLength)}};
        geode::prelude::CCPoint late[2] = {
            {static_cast<float>(view.x - nx * half - axisX * kBackLength),
             static_cast<float>(view.y - ny * half - axisY * kBackLength)},
            {static_cast<float>(view.x - nx * half + axisX * kForwardLength),
             static_cast<float>(view.y - ny * half + axisY * kForwardLength)}};
        geode::prelude::ccColor4F band =
            EbStraightflyVisualizerImplementation::bandColor(0.10f * alpha);
        geode::prelude::CCPoint triA[3] = {early[0], late[0], late[1]};
        geode::prelude::CCPoint triB[3] = {early[0], late[1], early[1]};
        m_node->drawPolygon(triA, 3, band, 0.f, band);
        m_node->drawPolygon(triB, 3, band, 0.f, band);
        m_node->drawSegment(behind, ahead, 1.35f,
                            EbStraightflyVisualizerImplementation::lineColor(0.85f * alpha));
        m_node->drawDot(anchor, 3.2f,
                        EbStraightflyVisualizerImplementation::anchorColor(0.95f * alpha));
        ++m_drawnLines;
    }
}
