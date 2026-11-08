#include "trajectory_visualizer.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/assist/planning/assist_horizon.hpp"
#include "../../core/assist/planning/assist_pick_v2.hpp"
#include "../../core/learning/level_profile.hpp"
#include "../../core/sim/foresight/long_foresight.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../core/sim/sim_trust.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/perf_trace.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

geode::prelude::ccColor4F TrajectoryVisualizerImplementation::col(float result, float gameObject,
                                                                  float secondValue,
                                                                  float firstValue) {
    return {result, gameObject, secondValue, firstValue};
}

// Hold Jump from frame 0 for the whole sample (never release).
std::vector<ReplayEvent> TrajectoryVisualizerImplementation::makeHoldEvents() {
    return {ReplayEvent{0, 1, false, true}};
}

// Coast / not-hold: force Jump up from frame 0.
std::vector<ReplayEvent> TrajectoryVisualizerImplementation::makeCoastEvents() {
    return {ReplayEvent{0, 1, false, false}};
}

// Press assist: coast until delay, then hold Jump (delay 0 = hold now).
std::vector<ReplayEvent> TrajectoryVisualizerImplementation::makeDelayedHoldEvents(int delay) {
    if (!(delay <= 0)) {
        return {ReplayEvent{0, 1, false, false}, ReplayEvent{delay, 1, false, true}};
    }
    return TrajectoryVisualizerImplementation::makeHoldEvents();
}

// Drop near-duplicate points so DrawNode work stays cheap.
std::vector<geode::prelude::CCPoint>
TrajectoryVisualizerImplementation::decimate(std::vector<geode::prelude::CCPoint> const& pts,
                                             float minDistSq) {
    if (pts.size() < 3)
        return pts;
    std::vector<geode::prelude::CCPoint> out;
    out.reserve(pts.size() / 2 + 2);
    out.push_back(pts.front());
    for (size_t index = 1; index + 1 < pts.size(); ++index) {
        float const deltaX = pts[index].x - out.back().x;
        float const deltaY = pts[index].y - out.back().y;
        if (deltaX * deltaX + deltaY * deltaY >= minDistSq)
            out.push_back(pts[index]);
    }
    out.push_back(pts.back());
    return out;
}

void TrajectoryVisualizerImplementation::markDeathX(geode::prelude::CCDrawNode* node,
                                                    ShadowSimulator::PathSample const& s,
                                                    geode::prelude::ccColor4F candidate) {
    if (!node || !s.died || s.points.empty())
        return;
    auto pt = s.points.back();
    float result = 4.5f;
    node->drawSegment({pt.x - result, pt.y - result}, {pt.x + result, pt.y + result}, 1.1f,
                      candidate);
    node->drawSegment({pt.x - result, pt.y + result}, {pt.x + result, pt.y - result}, 1.1f,
                      candidate);
}

int TrajectoryVisualizerImplementation::surviveBinOfTicks(int ticks) {
    if (ticks < 24)
        return 0;
    if (ticks < 48)
        return 1;
    if (!(ticks < 96)) {
        return 3;
    }
    return 2;
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
    if (!m_enabled)
        return {};
    if (m_v2Live) {
        std::string text = fmt::format("traj v2 LF {}t", m_lastDrawCap);
        if (m_lfExtended)
            text += "+";
        if (m_predCount > 0) {
            text += fmt::format(" P={}", m_predCount);
        }
        if (m_bestDelay >= 0) {
            text += fmt::format(" d={}", m_bestDelay);
        }
        if (!m_trustLong)
            text += " short";
        return text;
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
    if (!m_node)
        return;
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
    if (m_node && m_node->getParent())
        m_node->clear();
    m_hasWindow = false;
}

void TrajectoryVisualizer::ensureNode(PlayLayer* layer) {
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
    m_node->setID("earlybuffer-trajectory"_spr);
    m_node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    if (layer->m_objectLayer) {
        layer->m_objectLayer->addChild(m_node, 9999);
    } else {
        layer->addChild(m_node, 9999);
    }
    m_node->retain();
}

void TrajectoryVisualizer::drawPath(geode::prelude::CCDrawNode* node,
                                    std::vector<geode::prelude::CCPoint> const& pts,
                                    geode::prelude::ccColor4F color, float width, int stride) {
    if (!node || pts.size() < 2)
        return;
    if (stride < 1)
        stride = 1;
    for (size_t index = stride; index < pts.size(); index += static_cast<size_t>(stride)) {
        size_t prev = index - static_cast<size_t>(stride);
        float tick = static_cast<float>(index) / static_cast<float>(pts.size());
        geode::prelude::ccColor4F candidate = color;
        candidate.a = color.a * (1.f - 0.45f * tick);
        node->drawSegment(pts[prev], pts[index], width, candidate);
    }
    if (pts.size() > 2) {
        size_t lastAligned = ((pts.size() - 1) / stride) * stride;
        if (lastAligned + 1 < pts.size()) {
            node->drawSegment(pts[lastAligned], pts.back(), width, color);
        }
    }
}

void TrajectoryVisualizer::drawBand(geode::prelude::CCDrawNode* node,
                                    std::vector<geode::prelude::CCPoint> const& early,
                                    std::vector<geode::prelude::CCPoint> const& late) {
    if (!node)
        return;
    size_t count = std::min(early.size(), late.size());
    if (count < 2)
        return;

    geode::prelude::ccColor4F fill =
        TrajectoryVisualizerImplementation::col(0.35f, 0.85f, 1.f, 0.10f);
    int step = std::max(2, static_cast<int>(count) / 16);
    for (size_t index = 0; index + static_cast<size_t>(step) < count;
         index += static_cast<size_t>(step)) {
        size_t otherIndex = index + static_cast<size_t>(step);
        geode::prelude::CCPoint firstValue[3] = {early[index], late[index], late[otherIndex]};
        node->drawPolygon(firstValue, 3, fill, 0.f, fill);
        geode::prelude::CCPoint secondValue[3] = {early[index], late[otherIndex],
                                                  early[otherIndex]};
        node->drawPolygon(secondValue, 3, fill, 0.f, fill);
    }
}

void TrajectoryVisualizer::drawPredWindow(geode::prelude::CCDrawNode* node,
                                          geode::prelude::CCPoint const& at,
                                          geode::prelude::ccColor4F stroke,
                                          geode::prelude::ccColor4F fill) {
    if (!node)
        return;
    float const halfW = 5.f;
    float const halfH = 12.f;
    geode::prelude::CCPoint triA[3] = {
        {at.x - halfW, at.y - halfH}, {at.x + halfW, at.y - halfH}, {at.x + halfW, at.y + halfH}};
    geode::prelude::CCPoint triB[3] = {
        {at.x - halfW, at.y - halfH}, {at.x + halfW, at.y + halfH}, {at.x - halfW, at.y + halfH}};
    node->drawPolygon(triA, 3, fill, 0.f, fill);
    node->drawPolygon(triB, 3, fill, 0.f, fill);
    node->drawSegment({at.x, at.y - halfH - 2.f}, {at.x, at.y + halfH + 2.f}, 1.0f, stroke);
    node->drawDot(at, 2.8f, stroke);
}

void TrajectoryVisualizer::onPhysicsTick(PlayLayer* layer, float) {
    if (!m_enabled || !layer || !m_node)
        return;
    if (ShadowSimulator::get().isSimulating())
        return;
    if (!layer->m_objectLayer)
        return;
    if (!m_node->getParent()) {
        detach(true);
        return;
    }

    ++m_ticksSinceRebuild;
    // Longer trust / v2 LF draws: rebuild a bit less often.
    int const period = (m_trustLong || m_v2Live) && m_lastDrawCap > 96 ? 20 : 16;
    if (m_ticksSinceRebuild < period)
        return;
    m_ticksSinceRebuild = 0;
    rebuild(layer);
}

void TrajectoryVisualizer::rebuild(PlayLayer* layer) {
    PerfTrace::Scope const scope("trajRebuild");
    if (!this->m_enabled || !this->m_node || !layer || !layer->m_player1)
        return;
    if (!this->m_node->getParent())
        return;
    if (ShadowSimulator::get().isSimulating())
        return;

    auto* player = layer->m_player1;
    if (player->m_isDead) {
        m_node->clear();
        return;
    }

    float deltaTime = AssistController::get().simDt();
    if (deltaTime <= 0.f)
        deltaTime = static_cast<float>(1.0 / AssistClock::kNominalTps);

    auto params = StageConfig::paramsFor(player);
    int liveLimit = params.overwrite ? params.ticks : StageConfig::ticksAll();
    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    bool const trustOk = SimTrust::get().okAt(x);
    float const capMul = StageConfig::autoTune() ? LevelProfile::get().capMul(kind) : 1.f;
    auto const caps = AssistHorizon::resolve(kind, trustOk, liveLimit, capMul, params.cap,
                                             StageConfig::lookAheadCap());

    bool const v2 = !StageConfig::assistPickLegacy();
    m_v2Live = v2;
    m_lfExtended = false;
    m_predCount = 0;
    m_bestDelay = -1;

    // v1: trajDraw. v2 Live debug: LongForesight scoreHorizon (min 48).
    int lookAhead = v2 ? std::max(LongForesight::kMinHorizon, caps.scoreHorizon) : caps.trajDraw;
    m_trustLong = trustOk;
    m_hasWindow = lookAhead > 0;

    int const baseLookAhead = lookAhead;
    // Skip resim when nearly still and base draw cap unchanged.
    if (std::fabs(x - m_lastRebuildX) < 2.f && baseLookAhead == m_lastBaseCap) {
        return;
    }

    int slot = ShadowSimulator::get().captureSnapshot(player);
    if (slot < 0)
        return;

    auto& sim = ShadowSimulator::get();

    auto holdRaw = sim.samplePath(layer, slot, TrajectoryVisualizerImplementation::makeHoldEvents(),
                                  deltaTime, lookAhead);
    auto coastRaw = sim.samplePath(
        layer, slot, TrajectoryVisualizerImplementation::makeCoastEvents(), deltaTime, lookAhead);

    // LongForesight parity: extend once if press/hold dies inside the window.
    if (v2 && holdRaw.died && holdRaw.deathFrame >= 0) {
        int const lookCap = StageConfig::lookAheadCap();
        int const hMax =
            lookCap > 0 ? std::max(lookAhead, lookCap)
                        : std::min(320, lookAhead + lookAhead / 2 + LongForesight::kExtendStep);
        int const h1 = LongForesight::extendHorizon(lookAhead, holdRaw.deathFrame, hMax);
        if (h1 > lookAhead) {
            lookAhead = h1;
            m_lfExtended = true;
            holdRaw =
                sim.samplePath(layer, slot, TrajectoryVisualizerImplementation::makeHoldEvents(),
                               deltaTime, lookAhead);
            coastRaw =
                sim.samplePath(layer, slot, TrajectoryVisualizerImplementation::makeCoastEvents(),
                               deltaTime, lookAhead);
        }
    }

    m_lastRebuildX = x;
    m_lastBaseCap = baseLookAhead;
    m_lastDrawCap = lookAhead;

    auto hold = TrajectoryVisualizerImplementation::decimate(holdRaw.points, 3.f * 3.f);
    auto coast = TrajectoryVisualizerImplementation::decimate(coastRaw.points, 3.f * 3.f);

    m_node->clear();

    // Soft band between hold and coast where both share samples.
    drawBand(m_node, hold, coast);

    // Main green = hold · Red = coast (not hold)
    drawPath(m_node, hold, TrajectoryVisualizerImplementation::col(0.15f, 1.f, 0.4f, 0.95f), 1.35f,
             1);
    drawPath(m_node, coast, TrajectoryVisualizerImplementation::col(1.f, 0.28f, 0.22f, 0.75f), 1.0f,
             1);

    auto p0 = player->getPosition();
    m_node->drawDot(p0, 3.f, TrajectoryVisualizerImplementation::col(1.f, 1.f, 1.f, 1.f));

    TrajectoryVisualizerImplementation::markDeathX(
        m_node, holdRaw, TrajectoryVisualizerImplementation::col(0.15f, 1.f, 0.4f, 0.7f));
    TrajectoryVisualizerImplementation::markDeathX(
        m_node, coastRaw, TrajectoryVisualizerImplementation::col(1.f, 0.28f, 0.22f, 0.7f));

    if (!v2)
        return;

    // --- 13b: yellow prediction windows + dark-green best foresight path ---
    // Sparse delay samples across the Live window (incl. deaths).
    int const limit = std::max(0, liveLimit);
    std::vector<int> delays;
    delays.reserve(8);
    delays.push_back(0);
    if (limit >= 1) {
        int const step = std::max(1, limit / 4);
        for (int delay = step; delay < limit; delay += step) {
            delays.push_back(delay);
            if (static_cast<int>(delays.size()) >= 6)
                break;
        }
        if (delays.back() != limit)
            delays.push_back(limit);
    }

    geode::prelude::ccColor4F const yellowStroke =
        TrajectoryVisualizerImplementation::col(1.f, 0.92f, 0.2f, 0.95f);
    geode::prelude::ccColor4F const yellowFill =
        TrajectoryVisualizerImplementation::col(1.f, 0.88f, 0.15f, 0.18f);
    geode::prelude::ccColor4F const yellowPath =
        TrajectoryVisualizerImplementation::col(1.f, 0.85f, 0.2f, 0.45f);
    geode::prelude::ccColor4F const darkGreen =
        TrajectoryVisualizerImplementation::col(0.05f, 0.55f, 0.22f, 0.95f);

    AssistPickV2::CandView bestView{};
    int bestDelay = -1;
    ShadowSimulator::PathSample bestRaw{};
    bool haveBest = false;
    float const lam = params.latencyWeight;

    auto considerBest = [&](int delay, ShadowSimulator::PathSample const& raw,
                            AssistPickV2::CandView const& view) {
        if (!haveBest) {
            haveBest = true;
            bestView = view;
            bestDelay = delay;
            bestRaw = raw;
            return;
        }
        // Survivors beat deaths; else foresightBetter / later death.
        if (view.viable && !bestView.viable) {
            bestView = view;
            bestDelay = delay;
            bestRaw = raw;
            return;
        }
        if (!view.viable && bestView.viable)
            return;
        if (view.viable) {
            if (AssistPickV2::foresightBetter(view, delay, bestView, bestDelay, lam)) {
                bestView = view;
                bestDelay = delay;
                bestRaw = raw;
            }
            return;
        }
        if (view.surviveTicks > bestView.surviveTicks ||
            (view.surviveTicks == bestView.surviveTicks && view.endX > bestView.endX + 0.5f)) {
            bestView = view;
            bestDelay = delay;
            bestRaw = raw;
        }
    };

    for (int delay : delays) {
        auto rawData =
            (delay == 0)
                ? holdRaw
                : sim.samplePath(layer, slot,
                                 TrajectoryVisualizerImplementation::makeDelayedHoldEvents(delay),
                                 deltaTime, lookAhead);

        // Jump / prediction window at the delay frame (or start).
        size_t index = 0;
        if (delay > 0 && rawData.points.size() > static_cast<size_t>(delay)) {
            index = static_cast<size_t>(delay);
        }
        if (!rawData.points.empty()) {
            drawPredWindow(m_node, rawData.points[index], yellowStroke, yellowFill);
            ++m_predCount;
        }

        // Yellow ghost for every candidate (including deaths).
        if (delay != 0) {
            auto pts = TrajectoryVisualizerImplementation::decimate(rawData.points, 4.f * 4.f);
            drawPath(m_node, pts, yellowPath, 0.85f, 1);
            if (rawData.died)
                TrajectoryVisualizerImplementation::markDeathX(m_node, rawData, yellowStroke);
        }

        AssistPickV2::CandView view{};
        view.viable = !rawData.died;
        view.surviveTicks = rawData.died ? std::max(0, rawData.deathFrame)
                                         : std::max(0, static_cast<int>(rawData.points.size()) - 1);
        view.surviveBin = TrajectoryVisualizerImplementation::surviveBinOfTicks(view.surviveTicks);
        view.endX = rawData.points.empty() ? x : rawData.points.back().x;
        considerBest(delay, rawData, view);
    }

    if (haveBest && bestDelay >= 0) {
        m_bestDelay = bestDelay;
        auto bestPts = TrajectoryVisualizerImplementation::decimate(bestRaw.points, 3.f * 3.f);
        drawPath(m_node, bestPts, darkGreen, 1.7f, 1);
        if (!bestRaw.points.empty()) {
            size_t index = 0;
            if (bestDelay > 0 && bestRaw.points.size() > static_cast<size_t>(bestDelay)) {
                index = static_cast<size_t>(bestDelay);
            }
            m_node->drawDot(bestRaw.points[index], 4.f, darkGreen);
        }
        if (bestRaw.died)
            TrajectoryVisualizerImplementation::markDeathX(m_node, bestRaw, darkGreen);
    }
}
