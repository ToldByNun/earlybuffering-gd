#include "TrajectoryVisualizer.hpp"
#include "../core/AssistClock.hpp"
#include "../core/AssistController.hpp"
#include "../core/AssistHorizon.hpp"
#include "../core/LevelProfile.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/SimTrust.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/PerfTrace.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

ccColor4F col(float r, float g, float b, float a = 0.95f) {
    return { r, g, b, a };
}

// Hold Jump from frame 0 for the whole sample (never release).
static std::vector<ReplayEvent> makeHoldEvents() {
    return { ReplayEvent{ 0, 1, false, true } };
}

// Coast / not-hold: force Jump up from frame 0.
static std::vector<ReplayEvent> makeCoastEvents() {
    return { ReplayEvent{ 0, 1, false, false } };
}

// Drop near-duplicate points so DrawNode work stays cheap.
static std::vector<CCPoint> decimate(std::vector<CCPoint> const& pts, float minDistSq) {
    if (pts.size() < 3) return pts;
    std::vector<CCPoint> out;
    out.reserve(pts.size() / 2 + 2);
    out.push_back(pts.front());
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        // Commit 15: must NOT be static — recompute each point.
        float const dx = pts[i].x - out.back().x;
        float const dy = pts[i].y - out.back().y;
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
    if (m_trustLong) {
        return fmt::format("traj H/C {}t", m_lastDrawCap);
    }
    return fmt::format("traj H/C {}t (short)", m_lastDrawCap);
}

void TrajectoryVisualizer::attach(PlayLayer* layer) {
    ensureNode(layer);
    m_ticksSinceRebuild = 999;
    m_lastRebuildX = -1e9f;
    m_lastDrawCap = -1;
    m_trustLong = false;
}

void TrajectoryVisualizer::detach(bool layerDying) {
    m_enabled = false;
    m_ticksSinceRebuild = 0;
    m_hasWindow = false;
    if (!m_node) return;
    if (layerDying) {
        m_node->release();
        m_node = nullptr;
        return;
    }
    if (m_node->getParent()) {
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
        float t = static_cast<float>(i) / static_cast<float>(pts.size());
        ccColor4F c = color;
        c.a = color.a * (1.f - 0.45f * t);
        node->drawSegment(pts[prev], pts[i], width, c);
    }
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
    // Longer trust draws: rebuild a bit less often (~12 Hz).
    int const period = m_trustLong && m_lastDrawCap > 96 ? 20 : 16;
    if (m_ticksSinceRebuild < period) return;
    m_ticksSinceRebuild = 0;
    rebuild(layer);
}

void TrajectoryVisualizer::rebuild(PlayLayer* layer) {
    PerfTrace::Scope const scope("trajRebuild");
    if (!this->m_enabled || !this->m_node || !layer || !layer->m_player1) return;
    if (!this->m_node->getParent()) return;
    if (ShadowSimulator::get().isSimulating()) return;

    auto* player = layer->m_player1;
    if (player->m_isDead) {
        m_node->clear();
        return;
    }

    float dt = AssistController::get().simDt();
    if (dt <= 0.f) dt = static_cast<float>(1.0 / AssistClock::kNominalTps);

    auto params = StageConfig::paramsFor(player);
    int liveLimit = params.overwrite ? params.ticks : StageConfig::ticksAll();
    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    bool const trustOk = SimTrust::get().okAt(x);
    float const capMul = StageConfig::autoTune()
        ? LevelProfile::get().capMul(kind)
        : 1.f;
    auto const caps = AssistHorizon::resolve(
        kind,
        trustOk,
        liveLimit,
        capMul,
        params.cap,
        StageConfig::lookAheadCap()
    );
    // Commit 15: draw = trust cap (scoreHorizon when trusted, short otherwise).
    int const lookAhead = caps.trajDraw;
    m_trustLong = trustOk;
    m_hasWindow = lookAhead > 0;

    // Skip resim when nearly still and draw cap unchanged.
    if (std::fabs(x - m_lastRebuildX) < 2.f && lookAhead == m_lastDrawCap) {
        return;
    }
    m_lastRebuildX = x;
    m_lastDrawCap = lookAhead;

    int slot = ShadowSimulator::get().captureSnapshot(player);
    if (slot < 0) return;

    auto& sim = ShadowSimulator::get();

    // Two lines: HOLD vs NOT-HOLD (coast). One may die earlier — that is fine.
    auto holdRaw = sim.samplePath(layer, slot, makeHoldEvents(), dt, lookAhead);
    auto coastRaw = sim.samplePath(layer, slot, makeCoastEvents(), dt, lookAhead);

    auto hold = decimate(holdRaw.points, 3.f * 3.f);
    auto coast = decimate(coastRaw.points, 3.f * 3.f);

    m_node->clear();

    // Soft band between hold and coast where both share samples.
    drawBand(m_node, hold, coast);

    // Green = hold · Red = coast (not hold)
    drawPath(m_node, hold, col(0.15f, 1.f, 0.4f, 0.95f), 1.35f, 1);
    drawPath(m_node, coast, col(1.f, 0.28f, 0.22f, 0.75f), 1.0f, 1);

    auto p0 = player->getPosition();
    m_node->drawDot(p0, 3.f, col(1.f, 1.f, 1.f, 1.f));

    auto markDeath = [&](ShadowSimulator::PathSample const& s, ccColor4F c) {
        if (!s.died || s.points.empty()) return;
        auto pt = s.points.back();
        float r = 4.5f;
        m_node->drawSegment({ pt.x - r, pt.y - r }, { pt.x + r, pt.y + r }, 1.1f, c);
        m_node->drawSegment({ pt.x - r, pt.y + r }, { pt.x + r, pt.y - r }, 1.1f, c);
    };
    if (holdRaw.died) markDeath(holdRaw, col(0.15f, 1.f, 0.4f, 0.7f));
    if (coastRaw.died) markDeath(coastRaw, col(1.f, 0.28f, 0.22f, 0.7f));
}
