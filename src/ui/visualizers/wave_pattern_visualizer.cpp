#include "../../app/class_manager.hpp"
#include "wave_pattern_visualizer.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../debug/perf_trace.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

geode::prelude::ccColor4F EbWavePatternVisualizerImplementation::pathColor(float alpha) {
    return {1.f, 0.55f, 0.18f, alpha};
}

geode::prelude::ccColor4F EbWavePatternVisualizerImplementation::nextClickColor(float alpha) {
    return {1.f, 0.95f, 0.35f, alpha};
}

geode::prelude::ccColor4F EbWavePatternVisualizerImplementation::laterClickColor(float alpha) {
    return {1.f, 0.75f, 0.45f, alpha};
}

geode::prelude::ccColor4F EbWavePatternVisualizerImplementation::ghostColor(float alpha) {
    return {1.f, 0.40f, 0.12f, alpha};
}

geode::prelude::ccColor4F EbWavePatternVisualizerImplementation::corridorLineColor(float alpha) {
    return {1.f, 0.62f, 0.22f, alpha};
}

geode::prelude::ccColor4F EbWavePatternVisualizerImplementation::corridorBandColor(float alpha) {
    return {1.f, 0.45f, 0.12f, alpha};
}

void EbWavePatternVisualizerImplementation::stepDirection(bool holding, bool sideways,
                                                          bool reversed, bool inverted,
                                                          double& dirX, double& dirY) {
    double const forward = reversed ? -1. : 1.;
    double slope = holding ? 1. : -1.;
    if (inverted)
        slope = -slope;
    if (sideways) {
        dirX = slope;
        dirY = forward;
    } else {
        dirX = forward;
        dirY = slope;
    }
}

void EbWavePatternVisualizerImplementation::travelAxis(bool sideways, bool reversed, double degrees,
                                                       double& axisX, double& axisY) {
    double const radians = degrees * std::numbers::pi / 180.;
    double const forward = reversed ? -1. : 1.;
    axisX = sideways ? std::sin(radians) : forward * std::cos(radians);
    axisY = sideways ? forward * std::cos(radians) : std::sin(radians);
}

EbWavePatternVisualizer& EbWavePatternVisualizer::get() {
    return EbClassManager::get().service<EbWavePatternVisualizer>();
}

void EbWavePatternVisualizer::setEnabled(bool on) {
    m_enabled = on;
    if (!on) {
        m_ticksSinceRebuild = 0;
        clear();
    }
}

std::string EbWavePatternVisualizer::statusHint() const {
    if (!m_enabled)
        return {};
    if (m_drawnPaths <= 0)
        return "wp-viz idle";
    return fmt::format("wp-viz {}e", m_drawnEdges);
}

void EbWavePatternVisualizer::attach(PlayLayer* layer) {
    ensureNode(layer);
    m_ticksSinceRebuild = 999;
}

void EbWavePatternVisualizer::detach(bool layerDying) {
    m_enabled = false;
    m_ticksSinceRebuild = 0;
    m_drawnPaths = 0;
    m_drawnEdges = 0;
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

void EbWavePatternVisualizer::clear() {
    if (m_node && m_node->getParent())
        m_node->clear();
    m_drawnPaths = 0;
    m_drawnEdges = 0;
}

void EbWavePatternVisualizer::ensureNode(PlayLayer* layer) {
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
    m_node->setID("earlybuffer-wave-pattern"_spr);
    m_node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    // Above straightfly (9995), below trajectory (9999).
    if (layer->m_objectLayer)
        layer->m_objectLayer->addChild(m_node, 9996);
    else
        layer->addChild(m_node, 9996);
    m_node->retain();
}

void EbWavePatternVisualizer::onPhysicsTick(PlayLayer* layer, float) {
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

void EbWavePatternVisualizer::rebuild(PlayLayer* layer) {
    EbPerfTrace::Scope const scope("wavePatternRebuild");
    if (!m_node)
        return;
    m_node->clear();
    m_drawnPaths = 0;
    m_drawnEdges = 0;
    PlayerObject* players[2] = {layer->m_player1, layer->m_player2};
    for (int slot = 0; slot < 2; ++slot) {
        auto* player = players[slot];
        if (!player || player->m_isDead)
            continue;
        auto const view = EbAssistController::get().wavePatternView(slot);
        if (!view.valid || !(view.speed > 1.))
            continue;

        float const alpha = static_cast<float>(std::clamp(view.confidence, 0.25, 1.));
        bool holding = view.holding;
        double expected = holding ? view.holdSeconds : view.releaseSeconds;
        double remain = expected - view.phaseSeconds;
        if (!(remain > 0.002))
            remain = expected;
        remain = std::clamp(remain, 0.002, std::max(expected, 0.002));

        double x = view.x;
        double y = view.y;
        if (player->m_position.x != 0.f || player->m_position.y != 0.f) {
            x = player->m_position.x;
            y = player->m_position.y;
        }

        // Average-travel corridor (out-of-bounds strips) — same geometry as Ship.
        if (view.corridorValid) {
            double axisX = 0.;
            double axisY = 0.;
            EbWavePatternVisualizerImplementation::travelAxis(
                view.sideways, view.reversed, view.targetTravelDegrees, axisX, axisY);
            double const half =
                std::max(4., view.bodySize * 0.5 * std::max(.25, view.corridorWidth));
            double const nx = -axisY;
            double const ny = axisX;
            double const lx = view.lineX;
            double const ly = view.lineY;
            geode::prelude::CCPoint early[2] = {
                {static_cast<float>(lx + nx * half - axisX * kCorridorBack),
                 static_cast<float>(ly + ny * half - axisY * kCorridorBack)},
                {static_cast<float>(lx + nx * half + axisX * kCorridorForward),
                 static_cast<float>(ly + ny * half + axisY * kCorridorForward)}};
            geode::prelude::CCPoint late[2] = {
                {static_cast<float>(lx - nx * half - axisX * kCorridorBack),
                 static_cast<float>(ly - ny * half - axisY * kCorridorBack)},
                {static_cast<float>(lx - nx * half + axisX * kCorridorForward),
                 static_cast<float>(ly - ny * half + axisY * kCorridorForward)}};
            geode::prelude::ccColor4F band =
                EbWavePatternVisualizerImplementation::corridorBandColor(0.10f * alpha);
            geode::prelude::CCPoint triA[3] = {early[0], late[0], late[1]};
            geode::prelude::CCPoint triB[3] = {early[0], late[1], early[1]};
            m_node->drawPolygon(triA, 3, band, 0.f, band);
            m_node->drawPolygon(triB, 3, band, 0.f, band);
            geode::prelude::CCPoint const behind{
                static_cast<float>(lx - axisX * kCorridorBack),
                static_cast<float>(ly - axisY * kCorridorBack)};
            geode::prelude::CCPoint const ahead{
                static_cast<float>(lx + axisX * kCorridorForward),
                static_cast<float>(ly + axisY * kCorridorForward)};
            m_node->drawSegment(
                behind, ahead, 1.25f,
                EbWavePatternVisualizerImplementation::corridorLineColor(0.80f * alpha));
            m_node->drawDot({static_cast<float>(lx), static_cast<float>(ly)}, 2.8f,
                            EbWavePatternVisualizerImplementation::corridorLineColor(0.9f * alpha));
        }

        double pathLength = 0.;
        int edges = 0;
        geode::prelude::CCPoint previous{static_cast<float>(x), static_cast<float>(y)};
        m_node->drawDot(previous, 2.6f,
                        EbWavePatternVisualizerImplementation::nextClickColor(0.55f * alpha));

        for (int edge = 0; edge < kMaxEdges && pathLength < kMaxPathLength; ++edge) {
            double dirX = 0.;
            double dirY = 0.;
            EbWavePatternVisualizerImplementation::stepDirection(
                holding, view.sideways, view.reversed, view.inverted, dirX, dirY);
            double const duration = edge == 0 ? remain : (holding ? view.holdSeconds : view.releaseSeconds);
            double const distance = view.speed * duration;
            if (!(distance > 0.5))
                break;
            x += dirX * distance;
            y += dirY * distance;
            pathLength += static_cast<float>(distance);
            geode::prelude::CCPoint const next{static_cast<float>(x), static_cast<float>(y)};

            float const width = edge == 0 ? 2.1f : 1.55f;
            m_node->drawSegment(previous, next, width,
                                EbWavePatternVisualizerImplementation::pathColor(
                                    (edge == 0 ? 0.92f : 0.70f) * alpha));

            // Body-width ghost parallel (one side) so the zig looks like a lane, not a line.
            double const nx = -dirY;
            double const ny = dirX;
            double const half = std::max(3., view.bodySize * 0.35);
            geode::prelude::CCPoint const ghostA{
                previous.x + static_cast<float>(nx * half),
                previous.y + static_cast<float>(ny * half)};
            geode::prelude::CCPoint const ghostB{
                next.x + static_cast<float>(nx * half), next.y + static_cast<float>(ny * half)};
            m_node->drawSegment(ghostA, ghostB, 1.f,
                                EbWavePatternVisualizerImplementation::ghostColor(0.18f * alpha));

            float const radius = edge == 0 ? 4.2f : 2.8f;
            auto const clickColor =
                edge == 0 ? EbWavePatternVisualizerImplementation::nextClickColor(0.95f * alpha)
                          : EbWavePatternVisualizerImplementation::laterClickColor(0.75f * alpha);
            m_node->drawDot(next, radius, clickColor);
            previous = next;
            ++edges;
            holding = !holding;
        }
        if (edges > 0) {
            ++m_drawnPaths;
            m_drawnEdges += edges;
        }
    }
}
