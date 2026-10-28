#include "TrajectoryVisualizer.hpp"
#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

ccColor4F col(float r, float g, float b, float a = 0.95f) {
    return { r, g, b, a };
}

static std::vector<ReplayEvent> makeClickEvents(int delay, bool tapRelease) {
    if (delay < 0) delay = 0;
    std::vector<ReplayEvent> e;
    e.push_back(ReplayEvent{ delay, 1, false, true });
    if (tapRelease) {
        e.push_back(ReplayEvent{ delay + 2, 1, false, false });
    }
    return e;
}

// Drop near-duplicate points so DrawNode work stays cheap.
static std::vector<CCPoint> decimate(std::vector<CCPoint> const& pts, float minDistSq) {
    if (pts.size() < 3) return pts;
    std::vector<CCPoint> out;
    out.reserve(pts.size() / 2 + 2);
    out.push_back(pts.front());
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        static float dx = pts[i].x - out.back().x;
        static float dy = pts[i].y - out.back().y;
        if (dx * dx + dy * dy >= minDistSq) out.push_back(pts[i]);
    }
    out.push_back(pts.back());
    return out;
}



TrajectoryVisualizer& TrajectoryVisualizer::get() {
    static TrajectoryVisualizer inst;
    return inst;
}

void TrajectoryVisualizer::setEnabled(bool on) {
    m_enabled = on;
    if (!on) {
        m_ticksSinceRebuild = 0;
        clear();
    }
}

void TrajectoryVisualizer::toggle() {
    setEnabled(!m_enabled);
    AssistLog::info(fmt::format("TRAJ: visualizer {}", m_enabled ? "ON" : "OFF"));
}

std::string TrajectoryVisualizer::statusHint() const {
    if (!m_enabled) return {};
    if (!m_hasWindow) return "traj G/R";
    if (m_fromBake) {
        return fmt::format("traj G..R [{:.0f}..{:.0f}]", m_lastWindowEarly, m_lastWindowLate);
    }
    return fmt::format("traj G..R 0..{:.0f}t", m_lastWindowLate);
}

void TrajectoryVisualizer::attach(PlayLayer* layer) {
    ensureNode(layer);
    m_ticksSinceRebuild = 999;
    m_lastRebuildX = -1e9f;
    m_lastDelayEarly = -1;
    m_lastDelayLate = -1;
}

void TrajectoryVisualizer::detach(bool layerDying) {
    m_enabled = false;
    m_ticksSinceRebuild = 0;
    m_hasWindow = false;
    if (!m_node) return;
    if (!layerDying && m_node->getParent()) {
        m_node->removeFromParent();
    }
    m_node->release();
    m_node = nullptr;
}

void TrajectoryVisualizer::clear() {
    if (m_node && m_node->getParent()) m_node->clear();
    m_hasWindow = false;
}

void TrajectoryVisualizer::ensureNode(PlayLayer* layer) {
    if (!layer) return;
    if (m_node && m_node->getParent()) return;
    if (m_node) {
        m_node->release();
        m_node = nullptr;
    }
    m_node = CCDrawNode::create();
    if (!m_node) return;
    m_node->setID("earlybuffer-trajectory"_spr);
    m_node->setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
    if (layer->m_objectLayer) {
        layer->m_objectLayer->addChild(m_node, 9999);
    }
    else {
        layer->addChild(m_node, 9999);
    }
    m_node->retain();
}

void TrajectoryVisualizer::drawPath(
    CCDrawNode* node,
    std::vector<CCPoint> const& pts,
    ccColor4F color,
    float width,
    int stride
) {
    if (!node || pts.size() < 2) return;
    if (stride < 1) stride = 1;
    for (size_t i = stride; i < pts.size(); i += static_cast<size_t>(stride)) {
        size_t prev = i - static_cast<size_t>(stride);
        // Fade slightly toward the tip so the near path reads stronger
        float t = static_cast<float>(i) / static_cast<float>(pts.size());
        ccColor4F c = color;
        c.a = color.a * (1.f - 0.45f * t);
        node->drawSegment(pts[prev], pts[i], width, c);
    }
    // Always connect last segment
    if (pts.size() > 2) {
        size_t lastAligned = ((pts.size() - 1) / stride) * stride;
        if (lastAligned + 1 < pts.size()) {
            node->drawSegment(pts[lastAligned], pts.back(), width, color);
        }
    }
}

void TrajectoryVisualizer::drawBand(
    CCDrawNode* node,
    std::vector<CCPoint> const& early,
    std::vector<CCPoint> const& late
) {
    if (!node) return;
    size_t n = std::min(early.size(), late.size());
    if (n < 2) return;

    // Soft cyan band = click window thickness (keep sparse — DrawNode polygons are costly)
    ccColor4F fill = col(0.35f, 0.85f, 1.f, 0.10f);
    int step = std::max(2, static_cast<int>(n) / 16);
    for (size_t i = 0; i + static_cast<size_t>(step) < n; i += static_cast<size_t>(step)) {
        size_t j = i + static_cast<size_t>(step);
        CCPoint a[3] = { early[i], late[i], late[j] };
        node->drawPolygon(a, 3, fill, 0.f, fill);
        CCPoint b[3] = { early[i], late[j], early[j] };
        node->drawPolygon(b, 3, fill, 0.f, fill);
    }
}

void TrajectoryVisualizer::onPhysicsTick(PlayLayer* layer, float) {
    if (!m_enabled || !layer || !m_node) return;
    if (ShadowSimulator::get().isSimulating()) return;
    if (!layer->m_objectLayer) return;
    if (!m_node->getParent()) {
        detach(true);
        return;
    }

    ++m_ticksSinceRebuild;
    // ~15 Hz rebuild (2 sims only) — snappier than full-horizon redraws
    if (m_ticksSinceRebuild < 16) return;
    m_ticksSinceRebuild = 0;
    rebuild(layer);
}

void TrajectoryVisualizer::rebuild(PlayLayer* layer) {
    if (!m_enabled || !m_node || !layer || !layer->m_player1) return;
    if (!m_node->getParent()) return;
    if (ShadowSimulator::get().isSimulating()) return;

    auto* player = layer->m_player1;
    if (player->m_isDead) {
        m_node->clear();
        return;
    }

    float dt = AssistController::get().lastDt();
    if (dt <= 0.f) dt = 1.f / 240.f;
    float tw = layer->m_gameState.m_timeWarp;
    if (tw > 0.f) dt /= tw;

    auto model = ShadowSimulator::modelFor(player, StageConfig::ticksAll());
    auto params = StageConfig::paramsFor(player);
    int liveLimit = params.overwrite ? params.ticks : StageConfig::ticksAll();

    auto& bake = BakeMap::get();
    double bakeTps = bake.loaded() && bake.data().tps > 0.0 ? bake.data().tps : 240.0;
    int approxFrame = static_cast<int>(std::lround(
        AssistController::get().attemptTick() * (bakeTps * dt)
    ));

    auto const* win = bake.loaded()
        ? bake.findNear(
            player->getPositionX(), approxFrame, true, false,
            StageConfig::kindOf(player)
          )
        : nullptr;

    int delayEarly = 0;
    int delayLate = std::max(0, liveLimit);
    m_fromBake = false;

    if (win) {
        m_fromBake = true;
        delayEarly = std::max(0, win->earliest - approxFrame);
        delayLate = std::max(delayEarly, win->latest - approxFrame);
        m_lastWindowEarly = static_cast<float>(win->earliest);
        m_lastWindowLate = static_cast<float>(win->latest);
        m_hasWindow = true;
    }
    else {
        m_lastWindowEarly = 0.f;
        m_lastWindowLate = static_cast<float>(delayLate);
        m_hasWindow = delayLate > 0;
    }

    float const x = player->getPositionX();
    // Skip resim when nearly still and the window hasn't changed
    if (std::fabs(x - m_lastRebuildX) < 2.f
        && delayEarly == m_lastDelayEarly
        && delayLate == m_lastDelayLate) {
        return;
    }
    m_lastRebuildX = x;
    m_lastDelayEarly = delayEarly;
    m_lastDelayLate = delayLate;

    // Short look-ahead: click + a bit after — not a full plan horizon
    int lookAhead = std::clamp(delayLate + 28, 32, 56);

    int slot = ShadowSimulator::get().captureSnapshot(player);
    if (slot < 0) return;

    auto& sim = ShadowSimulator::get();
    bool const tap = model.tapRelease;

    // Only two sims: earliest vs latest in the window
    auto earlyRaw = sim.samplePath(layer, slot, makeClickEvents(delayEarly, tap), dt, lookAhead);
    auto lateRaw = (delayLate == delayEarly)
        ? earlyRaw
        : sim.samplePath(layer, slot, makeClickEvents(delayLate, tap), dt, lookAhead);

    auto early = decimate(earlyRaw.points, 3.f * 3.f);
    auto late = decimate(lateRaw.points, 3.f * 3.f);

    m_node->clear();

    if (delayLate != delayEarly) {
        drawBand(m_node, early, late);
    }

    // Green = click early · Red = click late
    drawPath(m_node, early, col(0.15f, 1.f, 0.4f, 0.95f), 1.35f, 1);
    if (delayLate != delayEarly) {
        drawPath(m_node, late, col(1.f, 0.28f, 0.22f, 0.75f), 1.0f, 1);
    }

    // Player anchor
    auto p0 = player->getPosition();
    m_node->drawDot(p0, 3.f, col(1.f, 1.f, 1.f, 1.f));

    // Click moment from raw samples (frame-accurate, not decimated guess)
    auto clickAt = [&](std::vector<CCPoint> const& raw, int delay, ccColor4F c) {
        if (raw.empty()) return;
        size_t idx = static_cast<size_t>(std::clamp(delay, 0, static_cast<int>(raw.size()) - 1));
        auto pt = raw[idx];
        m_node->drawDot(pt, 3.5f, c);
        m_node->drawSegment({ pt.x, pt.y - 8.f }, { pt.x, pt.y + 8.f }, 0.9f, c);
    };
    clickAt(earlyRaw.points, delayEarly, col(0.15f, 1.f, 0.4f, 1.f));
    if (delayLate != delayEarly) {
        clickAt(lateRaw.points, delayLate, col(1.f, 0.28f, 0.22f, 1.f));
    }

    // Death = small X (only when that path dies)
    auto markDeath = [&](ShadowSimulator::PathSample const& s, ccColor4F c) {
        if (!s.died || s.points.empty()) return;
        auto pt = s.points.back();
        float r = 4.5f;
        m_node->drawSegment({ pt.x - r, pt.y - r }, { pt.x + r, pt.y + r }, 1.1f, c);
        m_node->drawSegment({ pt.x - r, pt.y + r }, { pt.x + r, pt.y - r }, 1.1f, c);
    };
    if (earlyRaw.died) markDeath(earlyRaw, col(0.15f, 1.f, 0.4f, 0.7f));
    if (lateRaw.died && delayLate != delayEarly) markDeath(lateRaw, col(1.f, 0.28f, 0.22f, 0.7f));
}
