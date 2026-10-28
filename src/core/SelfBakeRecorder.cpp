#include "SelfBakeRecorder.hpp"
#include "AssistController.hpp"
#include "AssistScopeGate.hpp"
#include "BakeMap.hpp"
#include "LevelProfile.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/LagProbe.hpp"
#include <fmt/format.h>

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

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

bool SelfBakeRecorder::canDrain(PlayLayer* layer) const {
    if (!layer) return false;
    if (!StageConfig::selfBake()) return false;
    if (BakeMap::get().isBaking()) return false;
    // Never burn frame time during death / fail anim — that was the hitch.
    if (auto* p = layer->m_player1) {
        if (p->m_isDead) return false;
    }
    return true;
}

void SelfBakeRecorder::onLevelInit(PlayLayer* layer) {
    this->m_cancelRefine = false;
    this->clearPending();
    this->m_refineQueue.clear();
    this->m_levelKey.clear();
    if (layer && layer->m_level) {
        this->m_levelKey = LevelProfile::makeKey(layer->m_level);
    }
}

void SelfBakeRecorder::onLevelQuit() {
    this->cancelRefine();
    this->clearPending();
    this->m_refineQueue.clear();
    this->m_levelKey.clear();
}

void SelfBakeRecorder::cancelRefine() {
    this->m_cancelRefine = true;
}

void SelfBakeRecorder::clearPending() {
    this->m_pending.clear();
}

void SelfBakeRecorder::noteVanillaInput(
    PlayLayer* layer, PlayerObject* player, bool player1,
    bool down, int attemptTick, float simDt
) {
    if (!this->shouldRecord(layer, player)) return;
    if (this->m_pending.size() >= kMaxPending) return;

    if (layer->m_level) {
        auto const key = LevelProfile::makeKey(layer->m_level);
        if (!this->m_levelKey.empty() && key != this->m_levelKey) {
            this->clearPending();
            this->m_refineQueue.clear();
            this->m_levelKey = key;
        }
        else if (this->m_levelKey.empty()) {
            this->m_levelKey = key;
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
    this->m_pending.push_back(s);
}

void SelfBakeRecorder::mergeSamples(std::vector<Sample>& samples, char const* reason) {
    if (samples.empty()) return;

    std::vector<BakedClick> clicks;
    clicks.reserve(samples.size());
    for (auto const& s : samples) {
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
    samples.clear();

    int const n = BakeMap::get().mergeSelfClicks(clicks);
    if (n > 0) {
        AssistLog::debug(fmt::format(
            "self-bake: merged {} click(s) ({}) → map +{}",
            clicks.size(),
            reason ? reason : "?",
            n
        ));
    }

    // Queue for slow refine (only when setting on). Cap queue growth.
    if (StageConfig::selfBakeRefine()) {
        for (auto const& c : clicks) {
            if (this->m_refineQueue.size() >= kMaxPending) break;
            this->m_refineQueue.push_back(c);
        }
    }
}

void SelfBakeRecorder::drainRefine(PlayLayer* layer) {
    if (!layer || this->m_refineQueue.empty()) return;
    if (!StageConfig::selfBakeRefine()) {
        this->m_refineQueue.clear();
        return;
    }
    if (AssistController::get().assistTouchedRun()) {
        // Assist polluted this attempt's start snapshot usefulness — drop queue.
        this->m_refineQueue.clear();
        return;
    }
    int const slot = AssistController::get().startSnapshotSlot();
    if (slot < 0) return;

    // One click per tick, tiny sim budget — spreads cost across frames.
    BakedClick click = this->m_refineQueue.front();
    this->m_refineQueue.erase(this->m_refineQueue.begin());

    std::vector<BakedClick> one{ click };
    this->m_cancelRefine = false;
    int const expanded = BakeMap::get().refineSelfWindows(
        layer,
        slot,
        one,
        StageConfig::bakeRadius(),
        kIdleRefineSimsPerTick,
        &this->m_cancelRefine
    );
    if (expanded > 0 || one[0].earliest != click.earliest || one[0].latest != click.latest) {
        BakeMap::get().mergeSelfClicks(one);
    }
}

void SelfBakeRecorder::onPhysicsTick(PlayLayer* layer) {
    if (!this->canDrain(layer)) return;

    // 1) Cheap: fold buffered samples into the bake map (point windows).
    if (!this->m_pending.empty()) {
        size_t const n = std::min(this->m_pending.size(), kIdleMergePerTick);
        std::vector<Sample> batch(
            this->m_pending.begin(),
            this->m_pending.begin() + static_cast<std::ptrdiff_t>(n)
        );
        this->m_pending.erase(
            this->m_pending.begin(),
            this->m_pending.begin() + static_cast<std::ptrdiff_t>(n)
        );
        LagProbe::Scope const scope("eb.selfBake.idleMerge");
        this->mergeSamples(batch, "idle");
    }

    // 2) Amortized refine — only when merge buffer is quiet.
    if (this->m_pending.empty() && !this->m_refineQueue.empty()) {
        LagProbe::Scope const scope("eb.selfBake.idleRefine");
        this->drainRefine(layer);
    }
}

void SelfBakeRecorder::flushMergeOnly(PlayLayer* layer, char const* reason) {
    (void)layer;
    if (this->m_pending.empty()) {
        LagProbe::get().mark("flushMergeOnly.empty", reason ? reason : "?");
        return;
    }
    if (BakeMap::get().isBaking()) {
        LagProbe::get().mark("flushMergeOnly.skippedBake", reason ? reason : "?");
        return;
    }
    LagProbe::Scope const scope("eb.selfBake.flushMergeOnly", reason ? reason : "?");
    this->mergeSamples(this->m_pending, reason);
    // Drop refine queue on forced flush — quit/complete shouldn't start sims.
    this->m_refineQueue.clear();
}

void SelfBakeRecorder::flushSegment(PlayLayer* layer, char const* reason, bool allowRefine) {
    // Hot-path compatibility shim: NEVER refine here. Merge-only, or no-op if
    // caller is reset/checkpoint (those should leave work to idle drain).
    (void)allowRefine;
    char const* r = reason ? reason : "?";
    // Death/reset/checkpoint must stay instant — leave buffer for idle drain.
    if (std::strcmp(r, "reset") == 0
        || std::strcmp(r, "checkpoint") == 0
        || std::strcmp(r, "death") == 0) {
        LagProbe::get().mark("flushSegment.deferred", fmt::format(
            "reason={} pending={} refineQ={}",
            r, this->m_pending.size(), this->m_refineQueue.size()
        ));
        return;
    }
    this->flushMergeOnly(layer, r);
}
