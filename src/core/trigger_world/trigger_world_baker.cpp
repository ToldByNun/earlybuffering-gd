#include "trigger_world_baker.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/lag_probe.hpp"
#include "../../debug/perf_trace.hpp"
#include "../assist/assist_controller.hpp"
#include "../learning/level_profile.hpp"
#include "baking/trigger_bake_job.hpp"
#include "baking/trigger_effect_baker.hpp"
#include "trigger_classify.hpp"
#include "trigger_world_store.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <string>
#include <vector>

TriggerWorldBaker& TriggerWorldBaker::get() {
    static TriggerWorldBaker inst;
    return inst;
}

TriggerWorldBaker::LivePose TriggerWorldBaker::poseFromObject(GameObject* object) {
    return TriggerEffectBaker::poseFromObject(object);
}

TriggerPose TriggerWorldBaker::toKey(int tick, LivePose const& live) {
    return TriggerEffectBaker::toKey(tick, live);
}

void TriggerWorldBaker::pushKey(TriggerTrack& track, int tick, LivePose const& live) {
    return TriggerEffectBaker::pushKey(track, tick, live);
}

int TriggerWorldBaker::durationTicks(float durationSec, double tps) {
    return TriggerEffectBaker::durationTicks(durationSec, tps);
}

bool TriggerWorldBaker::isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind) {
    return TriggerEffectBaker::isFollowLikeStatic(effect, kind);
}

cocos2d::CCPoint TriggerWorldBaker::groupCenter(int groupId,
                                                std::vector<GameObject*> const& objects,
                                                std::unordered_map<int, LivePose> const& live) {
    return TriggerEffectBaker::groupCenter(groupId, objects, live);
}

void TriggerWorldBaker::clearJob() {
    this->m_layer = nullptr;
    this->m_phase = Phase::Idle;
    this->m_childIndex = 0;
    this->m_childCount = 0;
    this->m_effectIndex = 0;
    this->m_secOuter = 0;
    this->m_secInner = 0;
    this->m_secSlot = 0;
    this->m_sectionExtras = 0;
    this->m_objects.clear();
    this->m_effects.clear();
    this->m_seenIds.clear();
    this->m_live.clear();
    this->m_tracks.clear();
    this->m_bakedEffects = 0;
    this->m_bakedStartTimed = 0;
    this->m_skippedFollow = 0;
    this->m_skippedDynamic = 0;
    this->m_skippedNonSpawn = 0;
    this->m_skippedTouch = 0;
    this->m_waitSlices = 0;
    this->m_collectIncomplete = false;
    this->m_levelKey.clear();
    this->m_tps = 240.0;
}

void TriggerWorldBaker::cancel() {
    this->m_cancel = true;
    if (this->m_running) {
        this->finishCancel();
        return;
    }
    this->m_progress = 0.f;
    this->m_status.clear();
    this->clearJob();
}

void TriggerWorldBaker::finishCancel() {
    this->m_running = false;
    this->m_progress = 0.f;
    this->m_status = "trigger-world: cancelled";
    this->clearJob();
    AssistLog::debug("TRIGGER-WORLD: bake cancelled");
}

void TriggerWorldBaker::finishOk() {
    this->m_running = false;
    this->m_progress = 1.f;
    this->m_lastBakedEffects = this->m_bakedEffects;
    this->m_lastBakedStartTimed = this->m_bakedStartTimed;
    this->m_lastSkippedFollow = this->m_skippedFollow;
    this->m_lastSkippedDynamic = this->m_skippedDynamic;
    this->m_lastSkippedNonSpawn = this->m_skippedNonSpawn;
    this->m_lastSkippedTouch = this->m_skippedTouch;
    this->m_lastChildCount = this->m_childCount;
    this->m_lastEffectCount = this->m_effects.size();
    this->m_lastSectionExtras = this->m_sectionExtras;
    this->m_lastCollectIncomplete = this->m_collectIncomplete;

    auto& store = TriggerWorldStore::get();
    size_t const tracks = store.trackCount();
    // Incomplete collect (0 children at init) must not poison disk cache.
    if (tracks == 0 && this->m_collectIncomplete) {
        store.clearDirty();
        store.discardCacheFile();
        this->m_status = "trigger-world: empty (collect incomplete — will retry)";
        AssistLog::info("TRIGGER-WORLD: 0 tracks after incomplete collect — not caching; "
                        "kickIfStaleEmpty will retry");
        this->clearJob();
        return;
    }
    if (tracks == 0) {
        store.clearDirty();
        store.discardCacheFile();
        this->m_status =
            fmt::format("trigger-world: empty (baked={} startTimed={} skip follow={} dyn={} "
                        "touch={} effects={} children={})",
                        this->m_lastBakedEffects, this->m_lastBakedStartTimed,
                        this->m_lastSkippedFollow, this->m_lastSkippedDynamic,
                        this->m_lastSkippedTouch, this->m_lastEffectCount, this->m_lastChildCount);
        AssistLog::info(fmt::format(
            "TRIGGER-WORLD: EMPTY after full collect — baked={} startTimed={} "
            "skip follow={} dynamic={} touch={} children={} effects={} sectionExtras={}",
            this->m_lastBakedEffects, this->m_lastBakedStartTimed, this->m_lastSkippedFollow,
            this->m_lastSkippedDynamic, this->m_lastSkippedTouch, this->m_lastChildCount,
            this->m_lastEffectCount, this->m_lastSectionExtras));
        this->clearJob();
        return;
    }

    this->m_status = store.statusLine();
    AssistLog::info(fmt::format(
        "TRIGGER-WORLD: baked {} effects → {} tracks ({} ready) @ {:.0f} tps "
        "(startTimed={} skip follow={} dynamic={} touch={} children={} "
        "collectedEffects={} sectionExtras={} softcap={})",
        this->m_bakedEffects, tracks, store.readyTrackCount(), this->m_tps, this->m_bakedStartTimed,
        this->m_skippedFollow, this->m_skippedDynamic, this->m_skippedTouch, this->m_lastChildCount,
        this->m_lastEffectCount, this->m_sectionExtras, store.softcapHit() ? 1 : 0));
    this->clearJob();
}

std::string TriggerWorldBaker::statusLine() const {
    if (this->m_running) {
        return fmt::format("trigger-world: building {:.0f}%", this->m_progress * 100.f);
    }
    if (!(!this->m_status.empty())) {
        return TriggerWorldStore::get().statusLine();
    }
    return this->m_status;
}

void TriggerWorldBaker::loadOrBuild(PlayLayer* layer) {
    this->cancel();
    if (!layer || !layer->m_level) {
        TriggerWorldStore::get().clear();
        return;
    }
    if (!StageConfig::triggerBake()) {
        TriggerWorldStore::get().clear();
        this->m_status.clear();
        return;
    }

    {
        LagProbe::Scope const lagScope("eb.triggerBake.load");
        TriggerWorldStore::get().loadForLevel(layer->m_level);
    }

    if (TriggerWorldStore::get().hasTracks()) {
        this->m_status = TriggerWorldStore::get().statusLine();
        AssistLog::debug(fmt::format("TRIGGER-WORLD: cache hit ({})", this->m_status));
        return;
    }

    this->start(layer);
}

void TriggerWorldBaker::resumeIfEmpty(PlayLayer* layer) {
    if (!layer || !StageConfig::triggerBake())
        return;
    if (this->m_running)
        return;
    auto& store = TriggerWorldStore::get();
    if (!store.loaded() || store.hasTracks())
        return;
    AssistLog::debug("TRIGGER-WORLD: resume build after empty cancel");
    this->start(layer);
}

void TriggerWorldBaker::kickIfStaleEmpty(PlayLayer* layer) {
    if (!layer || !StageConfig::triggerBake())
        return;
    if (this->m_running)
        return;
    auto& store = TriggerWorldStore::get();
    if (!store.loaded() || store.hasTracks())
        return;
    size_t children = 0;
    if (layer->m_objectLayer) {
        if (auto* ch = layer->m_objectLayer->getChildren()) {
            children = static_cast<size_t>(ch->count());
        }
    }
    if (children == 0 && layer->m_sections.empty())
        return;
    AssistLog::info(
        fmt::format("TRIGGER-WORLD: stale empty with {} objectLayer children — rebuild", children));
    this->start(layer);
}

bool TriggerWorldBaker::waitUntilReady(PlayLayer* layer, int wallMs, std::string& err) {
    err.clear();
    if (!layer) {
        err = "no PlayLayer for TW wait";
        return false;
    }
    if (!StageConfig::triggerBake())
        return true;

    this->kickIfStaleEmpty(layer);
    if (!this->m_running && !TriggerWorldStore::get().hasTracks()) {
        this->resumeIfEmpty(layer);
    }

    if (wallMs < 100)
        wallMs = 100;
    auto const t0 = std::chrono::steady_clock::now();
    while (this->m_running) {
        this->tickSlice();
        auto const elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - t0)
                                             .count();
        if (elapsedMilliseconds >= wallMs) {
            err = fmt::format(
                "Trigger World still building after {}ms ({:.0f}%) — cancel bake or wait", wallMs,
                this->m_progress * 100.f);
            return false;
        }
        if (this->m_cancel) {
            err = "Trigger World build cancelled during bake wait";
            return false;
        }
    }

    auto& store = TriggerWorldStore::get();
    if (store.hasTracks())
        return true;

    // Loud empty: effects existed but nothing baked, or collect incomplete.
    if (this->m_lastCollectIncomplete) {
        err = "Trigger World empty — collect incomplete (objectLayer not ready). Reset and retry "
              "bake.";
        return false;
    }
    if (this->m_lastEffectCount > 0 && this->m_lastBakedEffects == 0) {
        err = fmt::format(
            "Trigger World empty after build (effects={} skip follow={} dyn={} touch={}). "
            "Movers not modeled — bake would lie.",
            this->m_lastEffectCount, this->m_lastSkippedFollow, this->m_lastSkippedDynamic,
            this->m_lastSkippedTouch);
        return false;
    }
    return true;
}

bool TriggerWorldBaker::start(PlayLayer* layer) {
    if (!layer)
        return false;
    if (!StageConfig::triggerBake())
        return false;
    if (this->m_running)
        return false;

    this->clearJob();
    this->m_cancel = false;
    this->m_running = true;
    this->m_progress = 0.f;
    this->m_status = "trigger-world: building";
    this->m_layer = layer;

    // Author tracks in sim TPS (1/m_simDt), never warped wall TPS.
    this->m_tps = AssistController::get().estimatedTps();
    if (this->m_tps < 60.0 || this->m_tps > 480.0) {
        this->m_tps = 240.0;
    }

    if (layer->m_level) {
        this->m_levelKey = LevelProfile::makeKey(layer->m_level);
    }
    if (this->m_levelKey.empty()) {
        this->m_levelKey = TriggerWorldStore::get().data().levelKey;
    }

    this->m_childCount = 0;
    this->m_childIndex = 0;
    if (layer->m_objectLayer) {
        if (auto* children = layer->m_objectLayer->getChildren()) {
            this->m_childCount = static_cast<size_t>(children->count());
            this->m_objects.reserve(this->m_childCount);
            this->m_effects.reserve(256);
        }
    }

    // Commit 4: wait for objects if init raced ahead of objectLayer populate.
    if (this->m_childCount == 0) {
        this->m_phase = Phase::WaitObjects;
        this->m_waitSlices = 0;
        AssistLog::debug("TRIGGER-WORLD: bake queued — waiting for objectLayer");
    } else {
        this->m_phase = Phase::Collect;
        AssistLog::debug(
            fmt::format("TRIGGER-WORLD: bake queued ({} children)", this->m_childCount));
    }
    return true;
}

void TriggerWorldBaker::publishPartial() {
    TriggerWorldData data;
    data.levelKey = this->m_levelKey;
    data.tps = this->m_tps;
    data.version = TriggerWorldStore::kFileVersion;
    data.tracks.reserve(this->m_tracks.size());
    for (auto& [id, track] : this->m_tracks) {
        (void)id;
        if (track.keys.empty())
            continue;
        TriggerTrack copy = track;
        std::sort(copy.keys.begin(), copy.keys.end(),
                  [](TriggerPose const& a, TriggerPose const& b) { return a.tick < b.tick; });
        data.tracks.push_back(std::move(copy));
    }
    TriggerWorldStore::get().setData(std::move(data));
}

void TriggerWorldBaker::considerCollectedObject(GameObject* object) {
    if (!object)
        return;
    if (geode::prelude::typeinfo_cast<PlayerObject*>(object))
        return;
    int const id = object->m_uniqueID;
    if (id == 0)
        return;
    if (!this->m_seenIds.insert(id).second)
        return;
    this->m_objects.push_back(object);
    if (auto* effect = geode::prelude::typeinfo_cast<EffectGameObject*>(object)) {
        this->m_effects.push_back(effect);
    }
}

TriggerTrack* TriggerWorldBaker::ensureTrack(GameObject* object, TriggerKind kind) {
    return TriggerEffectBaker::ensureTrack(*this, object, kind);
}

std::vector<GameObject*> TriggerWorldBaker::objectsInGroup(int groupId) const {
    return TriggerEffectBaker::objectsInGroup(*this, groupId);
}

void TriggerWorldBaker::bakeOneEffect(EffectGameObject* effect) {
    return TriggerEffectBaker::bakeOneEffect(*this, effect);
}

void TriggerWorldBaker::tickSlice() {
    return TriggerBakeJob::tickSlice(*this);
}
