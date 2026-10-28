#include "SelfBakeRecorder.hpp"
#include "AssistController.hpp"
#include "AssistScopeGate.hpp"
#include "BakeMap.hpp"
#include "LevelProfile.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <cmath>

using namespace geode::prelude;

SelfBakeRecorder& SelfBakeRecorder::get() {
    static SelfBakeRecorder inst;
    return inst;
}

bool SelfBakeRecorder::shouldRecord(PlayLayer* layer, PlayerObject* player) const {
    if (!layer || !player) return false;
    if (!StageConfig::selfBake()) return false;
    if (AssistScopeGate::isPlatformer(player)) return false;
    if (BakeMap::get().isBaking()) return false;
    if (layer->m_isPracticeMode) return true;
    return StageConfig::selfBakeNormal();
}

void SelfBakeRecorder::onLevelInit(PlayLayer* layer) {
    m_cancelRefine = false;
    clearPending();
    m_levelKey.clear();
    if (layer && layer->m_level) {
        m_levelKey = LevelProfile::makeKey(layer->m_level);
    }
}

void SelfBakeRecorder::onLevelQuit() {
    cancelRefine();
    clearPending();
    m_levelKey.clear();
}

void SelfBakeRecorder::cancelRefine() {
    m_cancelRefine = true;
}

void SelfBakeRecorder::clearPending() {
    m_pending.clear();
}

void SelfBakeRecorder::noteVanillaInput(
    PlayLayer* layer, PlayerObject* player, bool player1,
    bool down, int attemptTick, float simDt
) {
    if (!shouldRecord(layer, player)) return;
    if (m_pending.size() >= kMaxPending) return;

    if (layer->m_level) {
        auto const key = LevelProfile::makeKey(layer->m_level);
        if (!m_levelKey.empty() && key != m_levelKey) {
            clearPending();
            m_levelKey = key;
        }
        else if (m_levelKey.empty()) {
            m_levelKey = key;
        }
    }

    double tps = 240.0;
    auto const& bake = BakeMap::get();
    if (bake.loaded() && bake.data().tps > 0.0) tps = bake.data().tps;
    if (simDt <= 0.f) simDt = 1.f / 240.f;

    Sample s;
    s.frame = static_cast<int>(std::lround(attemptTick * (tps * simDt)));
    s.x = player->getPositionX();
    s.mode = StageConfig::kindOf(player);
    s.down = down;
    s.player2 = !player1;
    m_pending.push_back(s);
}

void SelfBakeRecorder::flushSegment(PlayLayer* layer, char const* reason, bool allowRefine) {
    if (m_pending.empty()) return;
    if (BakeMap::get().isBaking()) {
        AssistLog::debug("self-bake: flush skipped (bake running)");
        return;
    }

    std::vector<BakedClick> clicks;
    clicks.reserve(m_pending.size());
    for (auto const& s : m_pending) {
        BakedClick c;
        c.nominalFrame = s.frame;
        c.earliest = s.frame;
        c.latest = s.frame;
        c.down = s.down;
        c.player2 = s.player2;
        c.x = s.x;
        c.mode = s.mode;
        clicks.push_back(c);
    }
    clearPending();

    int expanded = 0;
    if (allowRefine && StageConfig::selfBake() && StageConfig::selfBakeRefine() && layer) {
        int const slot = AssistController::get().startSnapshotSlot();
        bool const assistTouched = AssistController::get().assistTouchedRun();
        if (slot >= 0 && !assistTouched) {
            m_cancelRefine = false;
            expanded = BakeMap::get().refineSelfWindows(
                layer,
                slot,
                clicks,
                StageConfig::bakeRadius(),
                kRefineBudgetSims,
                &m_cancelRefine
            );
            if (m_cancelRefine.load()) {
                AssistLog::debug("self-bake: refine cancelled — merging current windows");
            }
        }
        else if (assistTouched) {
            AssistLog::debug("self-bake: refine skipped (assist touched run)");
        }
        else {
            AssistLog::debug("self-bake: refine skipped (no start snapshot)");
        }
    }

    int const n = BakeMap::get().mergeSelfClicks(clicks);
    if (n > 0) {
        AssistLog::debug(fmt::format(
            "self-bake: flushed {} click(s) ({}) expanded={} → map +{}",
            clicks.size(),
            reason ? reason : "?",
            expanded,
            n
        ));
    }
}
