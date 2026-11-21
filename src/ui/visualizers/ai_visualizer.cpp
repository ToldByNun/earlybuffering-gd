#include "../../app/class_manager.hpp"
#include "ai_visualizer.hpp"
#include "../../core/learning/neural_grid.hpp"
#include "../../core/learning/neural_learning.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../debug/perf_trace.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

geode::prelude::ccColor4F EbAiVisualizerImplementation::solidColor(float alpha) {
    // OpenHack default solid ≈ (0, 0.247, 1)
    return {0.0f, 0.25f, 1.0f, alpha};
}

geode::prelude::ccColor4F EbAiVisualizerImplementation::hazardColor(float alpha) {
    return {1.0f, 0.0f, 0.0f, alpha};
}

geode::prelude::ccColor4F EbAiVisualizerImplementation::orbColor(float alpha) {
    return {0.15f, 0.95f, 0.35f, alpha};
}

geode::prelude::ccColor4F EbAiVisualizerImplementation::frameColor(float alpha) {
    return {0.55f, 0.35f, 0.95f, alpha};
}

geode::prelude::ccColor4F EbAiVisualizerImplementation::rankColor(int rank, float confidence,
                                                                  float alpha) {
    float const t = std::clamp(confidence, 0.0f, 1.0f);
    float const fade = 1.0f - 0.12f * static_cast<float>(std::clamp(rank, 0, 7));
    return {0.35f + 0.45f * t, 0.25f + 0.55f * fade, 0.95f, alpha * (0.45f + 0.55f * t)};
}

geode::prelude::ccColor4F EbAiVisualizerImplementation::armedColor(float alpha) {
    return {1.0f, 1.0f, 1.0f, alpha};
}

geode::prelude::ccColor4F EbAiVisualizerImplementation::trainPulseColor(float alpha) {
    return {0.35f, 0.95f, 0.55f, alpha};
}

EbAiVisualizer& EbAiVisualizer::get() {
    return EbClassManager::get().service<EbAiVisualizer>();
}

void EbAiVisualizer::setEnabled(bool on) {
    m_enabled = on;
    if (!on) {
        m_ticksSinceRebuild = 0;
        clear();
    }
}

std::string EbAiVisualizer::statusHint() const {
    if (!m_enabled)
        return {};
    auto const& viz = EbNeuralLearning::get().viz();
    if (!viz.valid)
        return "ai-viz idle";
    return fmt::format("ai-viz hb{} d{}", viz.hitboxCount, m_drawnCandidates);
}

void EbAiVisualizer::attach(PlayLayer* layer) {
    ensureNode(layer);
    m_ticksSinceRebuild = 999;
}

void EbAiVisualizer::detach(bool layerDying) {
    m_enabled = false;
    m_ticksSinceRebuild = 0;
    m_drawnCells = 0;
    m_drawnCandidates = 0;
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

void EbAiVisualizer::clear() {
    if (m_node && m_node->getParent())
        m_node->clear();
    m_drawnCells = 0;
    m_drawnCandidates = 0;
}

void EbAiVisualizer::ensureNode(PlayLayer* layer) {
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
    m_node->setID("earlybuffer-ai-viz"_spr);
    m_node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    if (layer->m_objectLayer)
        layer->m_objectLayer->addChild(m_node, 9992);
    else
        layer->addChild(m_node, 9992);
    m_node->retain();
}

void EbAiVisualizer::onPhysicsTick(PlayLayer* layer, float) {
    if (!m_enabled || !layer || !m_node)
        return;
    if (EbShadowSimulator::get().isSimulating())
        return;
    if (!layer->m_objectLayer)
        return;
    if (!m_node->getParent()) {
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

void EbAiVisualizer::rebuild(PlayLayer* layer) {
    EbPerfTrace::Scope const scope("aiVizRebuild");
    if (!m_node)
        return;
    m_node->clear();
    m_drawnCells = 0;
    m_drawnCandidates = 0;

    auto& neural = EbNeuralLearning::get();
    neural.refreshViz(layer);
    auto const& viz = neural.viz();
    if (!viz.valid)
        return;

    float const originX = viz.originX;
    float const originY = viz.originY;

    // Perception window frame only (cells are quantized — exact hitboxes below).
    geode::prelude::CCPoint frame[4] = {
        {originX, originY},
        {originX + EbNeuralGrid::kGridWidth, originY},
        {originX + EbNeuralGrid::kGridWidth, originY + EbNeuralGrid::kGridHeight},
        {originX, originY + EbNeuralGrid::kGridHeight}};
    m_node->drawPolygon(frame, 4, {0.f, 0.f, 0.f, 0.f}, 1.1f,
                        EbAiVisualizerImplementation::frameColor(0.55f));

    // Exact GD hitboxes 1:1 (same getObjectRect/OBB the scorer stamps from).
    for (int index = 0; index < viz.hitboxCount; ++index) {
        auto const& hit = viz.hitboxes[index];
        geode::prelude::CCPoint quad[4];
        for (int corner = 0; corner < 4; ++corner) {
            quad[corner].x = hit.cornerX[corner];
            quad[corner].y = hit.cornerY[corner];
        }
        geode::prelude::ccColor4F border = EbAiVisualizerImplementation::solidColor(0.95f);
        geode::prelude::ccColor4F fill = EbAiVisualizerImplementation::solidColor(0.22f);
        if (hit.channelBits & EbNeuralGrid::kChannelHazard) {
            border = EbAiVisualizerImplementation::hazardColor(0.95f);
            fill = EbAiVisualizerImplementation::hazardColor(0.28f);
        } else if (hit.channelBits & EbNeuralGrid::kChannelOrbPad) {
            border = EbAiVisualizerImplementation::orbColor(0.95f);
            fill = EbAiVisualizerImplementation::orbColor(0.22f);
        }
        m_node->drawPolygon(quad, 4, fill, 0.9f, border);
        ++m_drawnCells;
    }

    if (viz.playerHitboxValid) {
        geode::prelude::CCPoint playerQuad[4] = {
            {viz.playerAabbX, viz.playerAabbY},
            {viz.playerAabbX + viz.playerAabbW, viz.playerAabbY},
            {viz.playerAabbX + viz.playerAabbW, viz.playerAabbY + viz.playerAabbH},
            {viz.playerAabbX, viz.playerAabbY + viz.playerAabbH}};
        m_node->drawPolygon(playerQuad, 4, {1.0f, 1.0f, 0.0f, 0.18f}, 1.0f,
                            {1.0f, 1.0f, 0.0f, 0.95f});
    }

    geode::prelude::CCPoint const player{viz.playerX, viz.playerY};
    m_node->drawDot(player, 3.0f, EbAiVisualizerImplementation::frameColor(0.9f));

    float const unitsPerTick = 5.77f * viz.playerSpeed;
    float dirX = viz.goingLeft ? -1.0f : 1.0f;
    float dirY = 0.0f;
    if (viz.sideways) {
        dirX = 0.0f;
        dirY = viz.goingLeft ? -1.0f : 1.0f;
    }
    for (int index = 0; index < viz.rankedCount; ++index) {
        int const delay = viz.rankedDelays[index];
        float const conf = viz.rankedConfidence[index];
        float const dist = static_cast<float>(delay) * unitsPerTick;
        geode::prelude::CCPoint const at{viz.playerX + dirX * dist, viz.playerY + dirY * dist};
        float const radius = 2.4f + 2.2f * std::clamp(conf, 0.0f, 1.0f);
        m_node->drawDot(at, radius,
                        EbAiVisualizerImplementation::rankColor(index, conf, 0.95f));
        if (index == 0)
            m_node->drawDot(at, radius + 1.6f,
                            EbAiVisualizerImplementation::rankColor(0, conf, 0.35f));
        ++m_drawnCandidates;
    }

    if (viz.lastArmedDelay >= 0) {
        float const dist = static_cast<float>(viz.lastArmedDelay) * unitsPerTick;
        geode::prelude::CCPoint const armed{viz.playerX + dirX * dist, viz.playerY + dirY * dist};
        m_node->drawDot(armed, 5.5f, EbAiVisualizerImplementation::armedColor(0.85f));
        m_node->drawDot(armed, 2.2f,
                        EbAiVisualizerImplementation::rankColor(0, viz.lastArmedConfidence, 1.0f));
    }

    if (viz.training || viz.updates != m_lastUpdates) {
        m_node->drawDot(player, 7.0f, EbAiVisualizerImplementation::trainPulseColor(0.22f));
        if (viz.updates != m_lastUpdates && m_lastUpdates >= 0)
            m_node->drawDot(player, 10.0f, EbAiVisualizerImplementation::trainPulseColor(0.35f));
    }
    m_lastUpdates = viz.updates;
}
