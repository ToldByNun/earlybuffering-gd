#include "TrajectoryVisualizer.hpp"
#include "../core/AssistClock.hpp"
#include "../core/AssistController.hpp"
#include "../core/AssistHorizon.hpp"
#include "../core/AssistPickV2.hpp"
#include "../core/LevelProfile.hpp"
#include "../core/LongForesight.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../core/SimTrust.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/PerfTrace.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

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

// Press assist: coast until delay, then hold Jump (delay 0 = hold now).
static std::vector<ReplayEvent> makeDelayedHoldEvents(int delay) {
    if (delay <= 0) return makeHoldEvents();
    return {
        ReplayEvent{ 0, 1, false, false },
        ReplayEvent{ delay, 1, false, true }
    };
}

// Drop near-duplicate points so DrawNode work stays cheap.
static std::vector<CCPoint> decimate(std::vector<CCPoint> const& pts, float minDistSq) {
    if (pts.size() < 3) return pts;
    std::vector<CCPoint> out;
    out.reserve(pts.size() / 2 + 2);
    out.push_back(pts.front());
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        float const dx = pts[i].x - out.back().x;
        float const dy = pts[i].y - out.back().y;
        if (dx * dx + dy * dy >= minDistSq) out.push_back(pts[i]);
    }
    out.push_back(pts.back());
    return out;
}

static void markDeathX(
    CCDrawNode* node,
    ShadowSimulator::PathSample const& s,
    ccColor4F c
) {
    if (!node || !s.died || s.points.empty()) return;
    auto pt = s.points.back();
    float r = 4.5f;
    node->drawSegment({ pt.x - r, pt.y - r }, { pt.x + r, pt.y + r }, 1.1f, c);
    node->drawSegment({ pt.x - r, pt.y + r }, { pt.x + r, pt.y - r }, 1.1f, c);
}

static int surviveBinOfTicks(int ticks) {
    if (ticks < 24) return 0;
    if (ticks < 48) return 1;
    if (ticks < 96) return 2;
    return 3;
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
    if (m_v2Live) {
        std::string s = fmt::format(
            "traj v2 LF {}t",
            m_lastDrawCap
        );
        if (m_lfExtended) s += "+";
        if (m_predCount > 0) {
            s += fmt::format(" P={}", m_predCount);
        }
        if (m_bestDelay >= 0) {
            s += fmt::format(" d={}", m_bestDelay);
        }
        if (!m_trustLong) s += " short";
        return s;
    }
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
    m_lastBaseCap = -1;
    m_trustLong = false;
    m_v2Live = false;
    m_lfExtended = false;
    m_predCount = 0;
    m_bestDelay = -1;
}

void TrajectoryVisualizer::detach(bool layerDying) {
    m_enabled = false;
    m_ticksSinceRebuild = 0;
    m_hasWindow = false;
    m_v2Live = false;
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

void TrajectoryVisualizer::drawPredWindow(
    CCDrawNode* node,
    CCPoint const& at,
    ccColor4F stroke,
    ccColor4F fill
) {
    if (!node) return;
    float const halfW = 5.f;
    float const halfH = 12.f;
    CCPoint triA[3] = {
        { at.x - halfW, at.y - halfH },
        { at.x + halfW, at.y - halfH },
        { at.x + halfW, at.y + halfH }
    };
    CCPoint triB[3] = {
        { at.x - halfW, at.y - halfH },
        { at.x + halfW, at.y + halfH },
        { at.x - halfW, at.y + halfH }
    };
    node->drawPolygon(triA, 3, fill, 0.f, fill);
    node->drawPolygon(triB, 3, fill, 0.f, fill);
    node->drawSegment(
        { at.x, at.y - halfH - 2.f },
        { at.x, at.y + halfH + 2.f },
        1.0f,
        stroke
    );
    node->drawDot(at, 2.8f, stroke);
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
    // Longer trust / v2 LF draws: rebuild a bit less often.
    int const period = (m_trustLong || m_v2Live) && m_lastDrawCap > 96 ? 20 : 16;
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

    bool const v2 = !StageConfig::assistPickLegacy();
    m_v2Live = v2;
    m_lfExtended = false;
    m_predCount = 0;
    m_bestDelay = -1;

    // v1: trajDraw. v2 Live debug: LongForesight scoreHorizon (min 48).
    int lookAhead = v2
        ? std::max(LongForesight::kMinHorizon, caps.scoreHorizon)
        : caps.trajDraw;
    m_trustLong = trustOk;
    m_hasWindow = lookAhead > 0;

    int const baseLookAhead = lookAhead;
    // Skip resim when nearly still and base draw cap unchanged.
    if (std::fabs(x - m_lastRebuildX) < 2.f && baseLookAhead == m_lastBaseCap) {
        return;
    }

    int slot = ShadowSimulator::get().captureSnapshot(player);
    if (slot < 0) return;

    auto& sim = ShadowSimulator::get();

    auto holdRaw = sim.samplePath(layer, slot, makeHoldEvents(), dt, lookAhead);
    auto coastRaw = sim.samplePath(layer, slot, makeCoastEvents(), dt, lookAhead);

    // LongForesight parity: extend once if press/hold dies inside the window.
    if (v2 && holdRaw.died && holdRaw.deathFrame >= 0) {
        int const lookCap = StageConfig::lookAheadCap();
        int const hMax = lookCap > 0
            ? std::max(lookAhead, lookCap)
            : std::min(
                320,
                lookAhead + lookAhead / 2 + LongForesight::kExtendStep
            );
        int const h1 = LongForesight::extendHorizon(
            lookAhead, holdRaw.deathFrame, hMax
        );
        if (h1 > lookAhead) {
            lookAhead = h1;
            m_lfExtended = true;
            holdRaw = sim.samplePath(layer, slot, makeHoldEvents(), dt, lookAhead);
            coastRaw = sim.samplePath(layer, slot, makeCoastEvents(), dt, lookAhead);
        }
    }

    m_lastRebuildX = x;
    m_lastBaseCap = baseLookAhead;
    m_lastDrawCap = lookAhead;

    auto hold = decimate(holdRaw.points, 3.f * 3.f);
    auto coast = decimate(coastRaw.points, 3.f * 3.f);

    m_node->clear();

    // Soft band between hold and coast where both share samples.
    drawBand(m_node, hold, coast);

    // Main green = hold · Red = coast (not hold)
    drawPath(m_node, hold, col(0.15f, 1.f, 0.4f, 0.95f), 1.35f, 1);
    drawPath(m_node, coast, col(1.f, 0.28f, 0.22f, 0.75f), 1.0f, 1);

    auto p0 = player->getPosition();
    m_node->drawDot(p0, 3.f, col(1.f, 1.f, 1.f, 1.f));

    markDeathX(m_node, holdRaw, col(0.15f, 1.f, 0.4f, 0.7f));
    markDeathX(m_node, coastRaw, col(1.f, 0.28f, 0.22f, 0.7f));

    if (!v2) return;

    // --- 13b: yellow prediction windows + dark-green best foresight path ---
    // Sparse delay samples across the Live window (incl. deaths).
    int const limit = std::max(0, liveLimit);
    std::vector<int> delays;
    delays.reserve(8);
    delays.push_back(0);
    if (limit >= 1) {
        int const step = std::max(1, limit / 4);
        for (int d = step; d < limit; d += step) {
            delays.push_back(d);
            if (static_cast<int>(delays.size()) >= 6) break;
        }
        if (delays.back() != limit) delays.push_back(limit);
    }

    ccColor4F const yellowStroke = col(1.f, 0.92f, 0.2f, 0.95f);
    ccColor4F const yellowFill = col(1.f, 0.88f, 0.15f, 0.18f);
    ccColor4F const yellowPath = col(1.f, 0.85f, 0.2f, 0.45f);
    ccColor4F const darkGreen = col(0.05f, 0.55f, 0.22f, 0.95f);

    AssistPickV2::CandView bestView{};
    int bestDelay = -1;
    ShadowSimulator::PathSample bestRaw{};
    bool haveBest = false;
    float const lam = params.latencyWeight;

    auto considerBest = [&](
        int d,
        ShadowSimulator::PathSample const& raw,
        AssistPickV2::CandView const& view
    ) {
        if (!haveBest) {
            haveBest = true;
            bestView = view;
            bestDelay = d;
            bestRaw = raw;
            return;
        }
        // Survivors beat deaths; else foresightBetter / later death.
        if (view.viable && !bestView.viable) {
            bestView = view;
            bestDelay = d;
            bestRaw = raw;
            return;
        }
        if (!view.viable && bestView.viable) return;
        if (view.viable) {
            if (AssistPickV2::foresightBetter(
                    view, d, bestView, bestDelay, lam
                )) {
                bestView = view;
                bestDelay = d;
                bestRaw = raw;
            }
            return;
        }
        if (view.surviveTicks > bestView.surviveTicks
            || (view.surviveTicks == bestView.surviveTicks
                && view.endX > bestView.endX + 0.5f)) {
            bestView = view;
            bestDelay = d;
            bestRaw = raw;
        }
    };

    for (int d : delays) {
        auto raw = (d == 0)
            ? holdRaw
            : sim.samplePath(layer, slot, makeDelayedHoldEvents(d), dt, lookAhead);

        // Jump / prediction window at the delay frame (or start).
        size_t idx = 0;
        if (d > 0 && raw.points.size() > static_cast<size_t>(d)) {
            idx = static_cast<size_t>(d);
        }
        if (!raw.points.empty()) {
            drawPredWindow(m_node, raw.points[idx], yellowStroke, yellowFill);
            ++m_predCount;
        }

        // Yellow ghost for every candidate (including deaths).
        if (d != 0) {
            auto pts = decimate(raw.points, 4.f * 4.f);
            drawPath(m_node, pts, yellowPath, 0.85f, 1);
            if (raw.died) markDeathX(m_node, raw, yellowStroke);
        }

        AssistPickV2::CandView view{};
        view.viable = !raw.died;
        view.surviveTicks = raw.died
            ? std::max(0, raw.deathFrame)
            : std::max(0, static_cast<int>(raw.points.size()) - 1);
        view.surviveBin = surviveBinOfTicks(view.surviveTicks);
        view.endX = raw.points.empty() ? x : raw.points.back().x;
        considerBest(d, raw, view);
    }

    if (haveBest && bestDelay >= 0) {
        m_bestDelay = bestDelay;
        auto bestPts = decimate(bestRaw.points, 3.f * 3.f);
        drawPath(m_node, bestPts, darkGreen, 1.7f, 1);
        if (!bestRaw.points.empty()) {
            size_t idx = 0;
            if (bestDelay > 0
                && bestRaw.points.size() > static_cast<size_t>(bestDelay)) {
                idx = static_cast<size_t>(bestDelay);
            }
            m_node->drawDot(bestRaw.points[idx], 4.f, darkGreen);
        }
        if (bestRaw.died) markDeathX(m_node, bestRaw, darkGreen);
    }
}
