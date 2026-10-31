#include "BakePathVisualizer.hpp"
#include "../core/BakeMap.hpp"
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
    if (!bake.loaded() || bake.data().clicks.empty()) return "bake-viz empty";
    return fmt::format("bake-viz {} near", this->m_drawnNear);
}

void BakePathVisualizer::attach(PlayLayer* layer) {
    this->ensureNode(layer);
    this->m_ticksSinceRebuild = 999;
    this->m_lastRebuildX = -1e9f;
    this->m_lastClickCount = -1;
}

void BakePathVisualizer::detach(bool layerDying) {
    this->m_enabled = false;
    this->m_ticksSinceRebuild = 0;
    this->m_drawnNear = 0;
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
    this->m_drawnNear = 0;
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
        this->m_drawnNear = 0;
        return;
    }

    auto& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty()) {
        this->m_node->clear();
        this->m_drawnNear = 0;
        return;
    }

    float const px = player->getPositionX();
    float const py = player->getPositionY();
    int const nClicks = static_cast<int>(bake.data().clicks.size());

    if (std::fabs(px - this->m_lastRebuildX) < 4.f
        && nClicks == this->m_lastClickCount
        && this->m_drawnNear > 0) {
        return;
    }
    this->m_lastRebuildX = px;
    this->m_lastClickCount = nClicks;

    // Collect P1 clicks near the player, keep closest N, draw left→right.
    // (Avoid identifier `near` — Win32 headers define it as a macro.)
    struct Mark {
        float x = 0.f;
        int earliest = 0;
        int latest = 0;
        bool down = true;
    };
    std::vector<Mark> marks;
    marks.reserve(64);
    for (auto const& c : bake.data().clicks) {
        if (c.player2) continue; // P1 map first
        if (std::fabs(c.x - px) > kLodMarginX) continue;
        marks.push_back(Mark{ c.x, c.earliest, c.latest, c.down });
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
    this->m_drawnNear = static_cast<int>(marks.size());
    if (marks.empty()) return;

    auto const stroke = bakeGreen(0.95f);
    auto const fill = bakeGreenDim(0.28f);
    auto const mid = bakeGreen(0.85f);

    // Path through nominal X positions.
    for (size_t i = 1; i < marks.size(); ++i) {
        float const y0 = py;
        this->m_node->drawSegment(
            { marks[i - 1].x, y0 },
            { marks[i].x, y0 },
            1.6f,
            stroke
        );
    }

    for (auto const& m : marks) {
        int const span = (std::max)(0, m.latest - m.earliest);
        // Window band width ~ frame span (capped); height fixed tick.
        float const halfW = (std::clamp)(3.f + static_cast<float>(span) * 0.35f, 3.f, 28.f);
        float const halfH = 14.f;

        // Soft green window rect (two triangles).
        CCPoint triA[3] = {
            { m.x - halfW, py - halfH },
            { m.x + halfW, py - halfH },
            { m.x + halfW, py + halfH }
        };
        CCPoint triB[3] = {
            { m.x - halfW, py - halfH },
            { m.x + halfW, py + halfH },
            { m.x - halfW, py + halfH }
        };
        this->m_node->drawPolygon(triA, 3, fill, 0.f, fill);
        this->m_node->drawPolygon(triB, 3, fill, 0.f, fill);

        // Nominal marker + vertical tick.
        this->m_node->drawDot({ m.x, py }, m.down ? 4.f : 3.f, mid);
        this->m_node->drawSegment(
            { m.x, py - halfH - 2.f },
            { m.x, py + halfH + 2.f },
            1.1f,
            stroke
        );
        // Earliest / latest brackets.
        this->m_node->drawSegment(
            { m.x - halfW, py - halfH },
            { m.x - halfW, py + halfH },
            0.8f,
            bakeGreen(0.7f)
        );
        this->m_node->drawSegment(
            { m.x + halfW, py - halfH },
            { m.x + halfW, py + halfH },
            0.8f,
            bakeGreen(0.7f)
        );
    }
}
