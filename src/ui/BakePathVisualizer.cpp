#include "BakePathVisualizer.hpp"
#include "../core/AssistClock.hpp"
#include "../core/AssistController.hpp"
#include "../core/BakeMap.hpp"
#include "../core/RoutePathStore.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../debug/PerfTrace.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

static ccColor4F bakeGreen(float a = 0.92f) {
    return { 0.12f, 0.92f, 0.38f, a };
}

static ccColor4F bakeGreenDim(float a = 0.35f) {
    return { 0.10f, 0.75f, 0.32f, a };
}

static ccColor4F bakePurple(float a = 0.92f) {
    return { 0.72f, 0.28f, 0.95f, a };
}

static ccColor4F bakePurpleDim(float a = 0.35f) {
    return { 0.55f, 0.18f, 0.78f, a };
}

// Past-latest: faint so the box does not invite a late press.
static ccColor4F bakeGone(float a = 0.18f) {
    return { 0.45f, 0.45f, 0.48f, a };
}

static ccColor4F routeCyan(float a = 0.8f) { return { 0.12f, 0.76f, 0.98f, a }; }
static ccColor4F routeCyanFill(float a = 0.11f) { return { 0.08f, 0.60f, 0.90f, a }; }

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
    if (!this->m_enabled) return {};
    auto& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) {
        return this->m_drawnRoute > 0 ? fmt::format("route-viz={}", this->m_drawnRoute) : "bake-viz empty";
    }
    return fmt::format(
        "bake-viz P={} R={} route={}", this->m_drawnPress, this->m_drawnRelease, this->m_drawnRoute
    );
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
    this->m_drawnRoute = 0;
    if (!this->m_node) return;
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
    if (this->m_node && this->m_node->getParent()) this->m_node->clear();
    this->m_drawnPress = 0;
    this->m_drawnRelease = 0;
    this->m_drawnRoute = 0;
}

void BakePathVisualizer::ensureNode(PlayLayer* layer) {
    if (!layer) return;
    if (this->m_node && this->m_node->getParent()) return;
    if (this->m_node) {
        this->m_node->release();
        this->m_node = nullptr;
    }
    this->m_node = CCDrawNode::create();
    if (!this->m_node) return;
    this->m_node->setID("earlybuffer-bake-path"_spr);
    this->m_node->setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
    // Above trajectory (9999) so bake coverage stays readable when both on.
    if (layer->m_objectLayer) {
        layer->m_objectLayer->addChild(this->m_node, 10000);
    }
    else {
        layer->addChild(this->m_node, 10000);
    }
    this->m_node->retain();
}

void BakePathVisualizer::onPhysicsTick(PlayLayer* layer, float) {
    if (!this->m_enabled || !layer || !this->m_node) return;
    if (ShadowSimulator::get().isSimulating()) return;
    if (!layer->m_objectLayer) return;
    if (!this->m_node->getParent()) {
        this->detach(true);
        return;
    }

    ++this->m_ticksSinceRebuild;
    if (this->m_ticksSinceRebuild < kRebuildEveryTicks) return;
    this->m_ticksSinceRebuild = 0;
    this->rebuild(layer);
}

void BakePathVisualizer::rebuild(PlayLayer* layer) {
    PerfTrace::Scope const scope("bakePathRebuild");
    if (!this->m_enabled || !this->m_node || !layer || !layer->m_player1) return;
    if (!this->m_node->getParent()) return;
    if (ShadowSimulator::get().isSimulating()) return;

    auto* player = layer->m_player1;
    if (player->m_isDead) {
        this->m_node->clear();
        this->m_drawnPress = 0;
        this->m_drawnRelease = 0;
        return;
    }

    auto& bake = BakeMap::get();
    auto const& routeStore = RoutePathStore::get();
    bool const haveBake = bake.loaded() && !bake.data().clicks.empty();
    bool const haveRoute = routeStore.loaded() && !routeStore.path().empty();
    if (!haveBake && !haveRoute) {
        this->m_node->clear();
        this->m_drawnPress = 0;
        this->m_drawnRelease = 0;
        return;
    }

    float const px = player->getPositionX();
    float const py = player->getPositionY();
    auto const liveMode = StageConfig::kindOf(player);
    bool const showAllReleases = releaseWindowMode(liveMode);
    int const nClicks = haveBake ? static_cast<int>(bake.data().clicks.size()) : 0;

    double const tps = haveBake && bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    float simDt = AssistController::get().estimatedTps() > 1.0
        ? static_cast<float>(1.0 / AssistController::get().estimatedTps())
        : AssistClock::kNominalSimDt;
    // Prefer live simDt when assist has one.
    // (estimatedTps is the stable overlay clock; identity at 240.)
    int const approxFrame = AssistClock::get().remapTick(
        tps, AssistController::get().attemptTick(), simDt
    );

    if (std::fabs(px - this->m_lastRebuildX) < 4.f
        && nClicks == this->m_lastClickCount
        && liveMode == this->m_lastMode
        && std::abs(approxFrame - this->m_lastApproxFrame) < 4
        && (this->m_drawnPress + this->m_drawnRelease) > 0) {
        return;
    }
    this->m_lastRebuildX = px;
    this->m_lastClickCount = nClicks;
    this->m_lastMode = liveMode;
    this->m_lastApproxFrame = approxFrame;

    // Collect P1 clicks near the player, keep closest N, draw left→right.
    // (Avoid identifier `near` — Win32 headers define it as a macro.)
    struct Mark {
        float x = 0.f;
        int earliest = 0;
        int latest = 0;
        int nominal = 0;
        bool down = true;
    };
    std::vector<Mark> marks;
    marks.reserve(64);
    for (auto const& c : bake.data().clicks) {
        if (c.player2) continue; // P1 map first
        if (std::fabs(c.x - px) > kLodMarginX) continue;
        // Drop windows that are well past latest — drawing them invites late presses.
        if (approxFrame > c.latest + 2) continue;
        if (c.down) {
            marks.push_back(Mark{ c.x, c.earliest, c.latest, c.nominalFrame, true });
            continue;
        }
        // Purple release: always in wave/ship/swing/robot; else dash/hold tag only.
        if (!showAllReleases && !c.holdAssist) continue;
        marks.push_back(Mark{ c.x, c.earliest, c.latest, c.nominalFrame, false });
    }
    if (marks.size() > kMaxMarkers) {
        std::nth_element(
            marks.begin(),
            marks.begin() + static_cast<std::ptrdiff_t>(kMaxMarkers),
            marks.end(),
            [px](Mark const& a, Mark const& b) {
                return std::fabs(a.x - px) < std::fabs(b.x - px);
            }
        );
        marks.resize(kMaxMarkers);
    }
    std::sort(marks.begin(), marks.end(), [](Mark const& a, Mark const& b) {
        return a.x < b.x;
    });

    this->m_node->clear();
    this->m_drawnPress = 0;
    this->m_drawnRelease = 0;
    this->m_drawnRoute = 0;

    // Cyan filled corridor is intentionally drawn before click windows. It is
    // stored pose data, not a prediction; RouteAssist consumes it next.
    if (haveRoute) {
        auto const& samples = routeStore.path().samples;
        for (size_t i = 1; i < samples.size(); ++i) {
            auto const& a = samples[i - 1];
            auto const& b = samples[i];
            if (b.gapBefore) continue;
            if (b.x < px - kLodMarginX || a.x > px + kLodMarginX) continue;
            CCPoint top[3] = {{a.x, a.yHi}, {b.x, b.yHi}, {b.x, b.yLo}};
            CCPoint bottom[3] = {{a.x, a.yHi}, {b.x, b.yLo}, {a.x, a.yLo}};
            this->m_node->drawPolygon(top, 3, routeCyanFill(), 0.f, routeCyanFill());
            this->m_node->drawPolygon(bottom, 3, routeCyanFill(), 0.f, routeCyanFill());
            this->m_node->drawSegment({a.x, a.y}, {b.x, b.y}, 1.2f, routeCyan());
            ++this->m_drawnRoute;
        }
    }
    if (marks.empty()) return;

    // ~px per bake frame (same scale as ShadowSimulator travel foresight).
    float pxPerFrame = 1.05f;
    {
        float const spd = std::max(0.5f, player->m_playerSpeed) * 311.2f * simDt;
        pxPerFrame = (std::clamp)(spd, 0.35f, 4.f);
    }

    // Windows only — no connector line between clicks (coverage, not a path).
    for (auto const& m : marks) {
        bool const isPress = m.down;
        if (isPress) ++this->m_drawnPress;
        else ++this->m_drawnRelease;

        bool const pastLatest = approxFrame > m.latest;
        bool const pastNominal = approxFrame > m.nominal;

        ccColor4F stroke;
        ccColor4F fill;
        ccColor4F mid;
        ccColor4F edge;
        if (pastLatest) {
            stroke = bakeGone(0.35f);
            fill = bakeGone(0.10f);
            mid = bakeGone(0.40f);
            edge = bakeGone(0.25f);
        }
        else if (isPress) {
            float const a = pastNominal ? 0.55f : 0.95f;
            stroke = bakeGreen(a);
            fill = bakeGreenDim(pastNominal ? 0.14f : 0.28f);
            mid = bakeGreen(pastNominal ? 0.50f : 0.85f);
            edge = bakeGreen(pastNominal ? 0.40f : 0.7f);
        }
        else {
            float const a = pastNominal ? 0.55f : 0.95f;
            stroke = bakePurple(a);
            fill = bakePurpleDim(pastNominal ? 0.14f : 0.28f);
            mid = bakePurple(pastNominal ? 0.50f : 0.85f);
            edge = bakePurple(pastNominal ? 0.40f : 0.7f);
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

        CCPoint triA[3] = {
            { x0, py - halfH },
            { x1, py - halfH },
            { x1, py + halfH }
        };
        CCPoint triB[3] = {
            { x0, py - halfH },
            { x1, py + halfH },
            { x0, py + halfH }
        };
        this->m_node->drawPolygon(triA, 3, fill, 0.f, fill);
        this->m_node->drawPolygon(triB, 3, fill, 0.f, fill);

        this->m_node->drawDot({ m.x, py }, pastLatest ? 2.5f : 4.f, mid);
        this->m_node->drawSegment(
            { m.x, py - halfH - 2.f },
            { m.x, py + halfH + 2.f },
            pastLatest ? 0.7f : 1.1f,
            stroke
        );
        this->m_node->drawSegment(
            { x0, py - halfH },
            { x0, py + halfH },
            0.8f,
            edge
        );
        this->m_node->drawSegment(
            { x1, py - halfH },
            { x1, py + halfH },
            0.8f,
            edge
        );
    }
}
