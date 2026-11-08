#include "bake_path_visualizer.hpp"
#include "../../core/assist/assist_clock.hpp"
#include "../../core/assist/assist_controller.hpp"
#include "../../core/bake/bake_map.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../debug/perf_trace.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

geode::prelude::ccColor4F BakePathVisualizerImplementation::bakeGreen(float firstValue) {
    return {0.12f, 0.92f, 0.38f, firstValue};
}

geode::prelude::ccColor4F BakePathVisualizerImplementation::bakeGreenDim(float firstValue) {
    return {0.10f, 0.75f, 0.32f, firstValue};
}

geode::prelude::ccColor4F BakePathVisualizerImplementation::bakePurple(float firstValue) {
    return {0.72f, 0.28f, 0.95f, firstValue};
}

geode::prelude::ccColor4F BakePathVisualizerImplementation::bakePurpleDim(float firstValue) {
    return {0.55f, 0.18f, 0.78f, firstValue};
}

// Past-latest: faint so the box does not invite a late press.
geode::prelude::ccColor4F BakePathVisualizerImplementation::bakeGone(float firstValue) {
    return {0.45f, 0.45f, 0.48f, firstValue};
}

bool BakePathVisualizer::releaseWindowMode(GameModeKind kind) {
    // Single source of truth: AssistController release gate (WU3).
    return AssistController::releaseAssistModeKind(kind);
}

BakePathVisualizer& BakePathVisualizer::get() {
    static BakePathVisualizer inst;
    return inst;
}

void BakePathVisualizer::setEnabled(bool on) {
    this->m_enabled = on;
    if (!on) {
        this->m_ticksSinceRebuild = 0;
        this->clear();
    }
}

std::string BakePathVisualizer::statusHint() const {
    if (!this->m_enabled)
        return {};
    auto& bake = BakeMap::get();
    if (!(!bake.loaded() || bake.data().clicks.empty())) {
        return fmt::format("bake-viz P={} R={}", this->m_drawnPress, this->m_drawnRelease);
    }
    return "bake-viz empty";
}

void BakePathVisualizer::attach(PlayLayer* layer) {
    this->ensureNode(layer);
    this->m_ticksSinceRebuild = 999;
    this->m_lastRebuildX = -1e9f;
    this->m_lastClickCount = -1;
    this->m_lastApproxFrame = -1;
}

void BakePathVisualizer::detach(bool layerDying) {
    this->m_enabled = false;
    this->m_ticksSinceRebuild = 0;
    this->m_drawnPress = 0;
    this->m_drawnRelease = 0;
    if (!this->m_node)
        return;
    if (layerDying) {
        this->m_node->release();
        this->m_node = nullptr;
        return;
    }
    if (this->m_node->getParent()) {
        this->m_node->removeFromParent();
    }
    this->m_node->release();
    this->m_node = nullptr;
}

void BakePathVisualizer::clear() {
    if (this->m_node && this->m_node->getParent())
        this->m_node->clear();
    this->m_drawnPress = 0;
    this->m_drawnRelease = 0;
}

void BakePathVisualizer::ensureNode(PlayLayer* layer) {
    if (!layer)
        return;
    if (this->m_node && this->m_node->getParent())
        return;
    if (this->m_node) {
        this->m_node->release();
        this->m_node = nullptr;
    }
    this->m_node = geode::prelude::CCDrawNode::create();
    if (!this->m_node)
        return;
    this->m_node->setID("earlybuffer-bake-path"_spr);
    this->m_node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    // Above trajectory (9999) so bake coverage stays readable when both on.
    if (layer->m_objectLayer) {
        layer->m_objectLayer->addChild(this->m_node, 10000);
    } else {
        layer->addChild(this->m_node, 10000);
    }
    this->m_node->retain();
}

void BakePathVisualizer::onPhysicsTick(PlayLayer* layer, float) {
    if (!this->m_enabled || !layer || !this->m_node)
        return;
    if (ShadowSimulator::get().isSimulating())
        return;
    if (!layer->m_objectLayer)
        return;
    if (!this->m_node->getParent()) {
        this->detach(true);
        return;
    }

    ++this->m_ticksSinceRebuild;
    if (this->m_ticksSinceRebuild < kRebuildEveryTicks)
        return;
    this->m_ticksSinceRebuild = 0;
    this->rebuild(layer);
}

void BakePathVisualizer::rebuild(PlayLayer* layer) {
    PerfTrace::Scope const scope("bakePathRebuild");
    if (!this->m_enabled || !this->m_node || !layer || !layer->m_player1)
        return;
    if (!this->m_node->getParent())
        return;
    if (ShadowSimulator::get().isSimulating())
        return;

    auto* player = layer->m_player1;
    if (player->m_isDead) {
        this->m_node->clear();
        this->m_drawnPress = 0;
        this->m_drawnRelease = 0;
        return;
    }

    auto& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) {
        this->m_node->clear();
        this->m_drawnPress = 0;
        this->m_drawnRelease = 0;
        return;
    }

    float const playerX = player->getPositionX();
    float const playerY = player->getPositionY();
    auto const liveMode = StageConfig::kindOf(player);
    bool const showAllReleases = releaseWindowMode(liveMode);
    int const nClicks = static_cast<int>(bake.data().clicks.size());

    double const tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    float simDt = AssistController::get().estimatedTps() > 1.0
                      ? static_cast<float>(1.0 / AssistController::get().estimatedTps())
                      : AssistClock::kNominalSimDt;
    // Prefer live simDt when assist has one.
    // (estimatedTps is the stable overlay clock; identity at 240.)
    int const approxFrame =
        AssistClock::get().remapTick(tps, AssistController::get().attemptTick(), simDt);

    if (std::fabs(playerX - this->m_lastRebuildX) < 4.f && nClicks == this->m_lastClickCount &&
        liveMode == this->m_lastMode && std::abs(approxFrame - this->m_lastApproxFrame) < 4 &&
        (this->m_drawnPress + this->m_drawnRelease) > 0) {
        return;
    }
    this->m_lastRebuildX = playerX;
    this->m_lastClickCount = nClicks;
    this->m_lastMode = liveMode;
    this->m_lastApproxFrame = approxFrame;

    // Collect P1 clicks near the player, keep closest N, draw left→right.
    // (Avoid identifier `near` — Win32 headers define it as a macro.)
    class Mark {
    public:
        float x = 0.f;
        int earliest = 0;
        int latest = 0;
        int nominal = 0;
        bool down = true;

    private:
    };
    std::vector<Mark> marks;
    marks.reserve(64);
    for (auto const& c : bake.data().clicks) {
        if (c.player2)
            continue; // P1 map first
        if (std::fabs(c.x - playerX) > kLodMarginX)
            continue;
        // Drop windows that are well past latest — drawing them invites late presses.
        if (approxFrame > c.latest + 2)
            continue;
        if (c.down) {
            marks.push_back(Mark{c.x, c.earliest, c.latest, c.nominalFrame, true});
            continue;
        }
        // Purple release: always in wave/ship/swing/robot; else dash/hold tag only.
        if (!showAllReleases && !c.holdAssist)
            continue;
        marks.push_back(Mark{c.x, c.earliest, c.latest, c.nominalFrame, false});
    }
    if (marks.size() > kMaxMarkers) {
        std::nth_element(marks.begin(), marks.begin() + static_cast<std::ptrdiff_t>(kMaxMarkers),
                         marks.end(), [playerX](Mark const& firstValue, Mark const& b) {
                             return std::fabs(firstValue.x - playerX) < std::fabs(b.x - playerX);
                         });
        marks.resize(kMaxMarkers);
    }
    std::sort(marks.begin(), marks.end(),
              [](Mark const& firstValue, Mark const& b) { return firstValue.x < b.x; });

    this->m_node->clear();
    this->m_drawnPress = 0;
    this->m_drawnRelease = 0;
    if (marks.empty())
        return;

    // ~px per bake frame (same scale as ShadowSimulator travel foresight).
    float pxPerFrame = 1.05f;
    {
        float const spd = std::max(0.5f, player->m_playerSpeed) * 311.2f * simDt;
        pxPerFrame = (std::clamp)(spd, 0.35f, 4.f);
    }

    // Windows only — no connector line between clicks (coverage, not a path).
    for (auto const& m : marks) {
        bool const isPress = m.down;
        if (isPress)
            ++this->m_drawnPress;
        else
            ++this->m_drawnRelease;

        bool const pastLatest = approxFrame > m.latest;
        bool const pastNominal = approxFrame > m.nominal;

        geode::prelude::ccColor4F stroke;
        geode::prelude::ccColor4F fill;
        geode::prelude::ccColor4F mid;
        geode::prelude::ccColor4F edge;
        if (pastLatest) {
            stroke = BakePathVisualizerImplementation::bakeGone(0.35f);
            fill = BakePathVisualizerImplementation::bakeGone(0.10f);
            mid = BakePathVisualizerImplementation::bakeGone(0.40f);
            edge = BakePathVisualizerImplementation::bakeGone(0.25f);
        } else if (isPress) {
            float const firstValue = pastNominal ? 0.55f : 0.95f;
            stroke = BakePathVisualizerImplementation::bakeGreen(firstValue);
            fill = BakePathVisualizerImplementation::bakeGreenDim(pastNominal ? 0.14f : 0.28f);
            mid = BakePathVisualizerImplementation::bakeGreen(pastNominal ? 0.50f : 0.85f);
            edge = BakePathVisualizerImplementation::bakeGreen(pastNominal ? 0.40f : 0.7f);
        } else {
            float const firstValue = pastNominal ? 0.55f : 0.95f;
            stroke = BakePathVisualizerImplementation::bakePurple(firstValue);
            fill = BakePathVisualizerImplementation::bakePurpleDim(pastNominal ? 0.14f : 0.28f);
            mid = BakePathVisualizerImplementation::bakePurple(pastNominal ? 0.50f : 0.85f);
            edge = BakePathVisualizerImplementation::bakePurple(pastNominal ? 0.40f : 0.7f);
        }

        // Asymmetric band: earliest ← nominal → latest in world X (not a fat square).
        float leftW = static_cast<float>((std::max)(0, m.nominal - m.earliest)) * pxPerFrame;
        float rightW = static_cast<float>((std::max)(0, m.latest - m.nominal)) * pxPerFrame;
        float const cap = BakeMap::windowHalfW(m.earliest, m.latest);
        leftW = (std::clamp)(leftW, 2.f, cap);
        rightW = (std::clamp)(rightW, 2.f, cap);
        // Once past nominal, shrink the trailing (late) edge so it stops looking valid.
        if (pastNominal && !pastLatest) {
            int const remain = (std::max)(0, m.latest - approxFrame);
            rightW = (std::min)(rightW, (std::max)(2.f, static_cast<float>(remain) * pxPerFrame));
        }
        if (pastLatest) {
            rightW = 2.f;
            leftW = (std::min)(leftW, 4.f);
        }

        float const halfH = 14.f;
        float const x0 = m.x - leftW;
        float const x1 = m.x + rightW;

        geode::prelude::CCPoint triA[3] = {
            {x0, playerY - halfH}, {x1, playerY - halfH}, {x1, playerY + halfH}};
        geode::prelude::CCPoint triB[3] = {
            {x0, playerY - halfH}, {x1, playerY + halfH}, {x0, playerY + halfH}};
        this->m_node->drawPolygon(triA, 3, fill, 0.f, fill);
        this->m_node->drawPolygon(triB, 3, fill, 0.f, fill);

        this->m_node->drawDot({m.x, playerY}, pastLatest ? 2.5f : 4.f, mid);
        this->m_node->drawSegment({m.x, playerY - halfH - 2.f}, {m.x, playerY + halfH + 2.f},
                                  pastLatest ? 0.7f : 1.1f, stroke);
        this->m_node->drawSegment({x0, playerY - halfH}, {x0, playerY + halfH}, 0.8f, edge);
        this->m_node->drawSegment({x1, playerY - halfH}, {x1, playerY + halfH}, 0.8f, edge);
    }
}
