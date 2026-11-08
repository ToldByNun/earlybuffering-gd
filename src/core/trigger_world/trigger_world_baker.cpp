#include "trigger_world_baker.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../../debug/lag_probe.hpp"
#include "../../debug/perf_trace.hpp"
#include "../assist/assist_controller.hpp"
#include "../learning/level_profile.hpp"
#include "trigger_classify.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fmt/format.h>

TriggerWorldBaker& TriggerWorldBaker::get() {
    static TriggerWorldBaker inst;
    return inst;
}

TriggerWorldBaker::LivePose TriggerWorldBaker::poseFromObject(GameObject* object) {
    LivePose p;
    if (!object)
        return p;
    p.x = object->getPositionX();
    p.y = object->getPositionY();
    p.rotation = object->getRotation();
    p.scaleX = object->getScaleX();
    p.scaleY = object->getScaleY();
    p.active = object->isVisible();
    p.objectId = object->m_objectID;
    return p;
}

TriggerPose TriggerWorldBaker::toKey(int tick, LivePose const& live) {
    TriggerPose k;
    k.tick = tick;
    k.x = live.x;
    k.y = live.y;
    k.rotation = live.rotation;
    k.scaleX = live.scaleX;
    k.scaleY = live.scaleY;
    k.active = live.active;
    return k;
}

void TriggerWorldBaker::pushKey(TriggerTrack& track, int tick, LivePose const& live) {
    // Soft key ceiling: decimate long movers, keep track — never silent drop whole track.
    if (track.keys.size() >= TriggerWorldStore::kSoftMaxKeysPerTrack) {
        TriggerWorldStore::decimateKeys(track.keys,
                                        TriggerWorldStore::kSoftMaxKeysPerTrack * 3 / 4);
        TriggerWorldStore::get().noteSoftcap("bake-keys");
    }
    if (track.keys.size() >= TriggerWorldStore::kHardMaxKeysPerTrack) {
        TriggerWorldStore::get().noteSoftcap("bake-keys-hard");
        return;
    }
    if (!track.keys.empty() && track.keys.back().tick == tick) {
        track.keys.back() = TriggerWorldBaker::toKey(tick, live);
        return;
    }
    if (!track.keys.empty() && track.keys.back().tick > tick)
        return;
    track.keys.push_back(TriggerWorldBaker::toKey(tick, live));
}

int TriggerWorldBaker::durationTicks(float durationSec, double tps) {
    if (durationSec <= 0.f)
        return 0;
    int const ticks = static_cast<int>(std::lround(static_cast<double>(durationSec) * tps));
    return std::max(0, ticks);
}

bool TriggerWorldBaker::isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind) {
    if (!effect)
        return false;
    if (kind != TriggerKind::StaticMove)
        return false;
    if (TriggerClassify::isFollowTriggerId(effect->m_objectID))
        return true;
    if (effect->m_useMoveTarget || effect->m_followCPP)
        return true;
    if (!(effect->m_isDynamicMode || effect->m_isDirectionFollowSnap360)) {
        return false;
    }
    return true;
}

cocos2d::CCPoint TriggerWorldBaker::groupCenter(int groupId,
                                                std::vector<GameObject*> const& objects,
                                                std::unordered_map<int, LivePose> const& live) {
    double sx = 0.0;
    double sy = 0.0;
    int count = 0;
    for (auto* object : objects) {
        if (!object || groupId <= 0)
            continue;
        if (!object->belongsToGroup(groupId))
            continue;
        auto it = live.find(object->m_uniqueID);
        if (it == live.end())
            continue;
        sx += it->second.x;
        sy += it->second.y;
        ++count;
    }
    if (!(count <= 0)) {
        return {static_cast<float>(sx / static_cast<double>(count)),
                static_cast<float>(sy / static_cast<double>(count))};
    }
    return {0.f, 0.f};
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
    if (!object)
        return nullptr;
    int const id = object->m_uniqueID;
    auto it = this->m_tracks.find(id);
    if (it != this->m_tracks.end())
        return &it->second;
    if (this->m_tracks.size() >= TriggerWorldStore::kSoftMaxTracks) {
        TriggerWorldStore::get().noteSoftcap("bake-tracks");
        return nullptr;
    }

    LivePose const pose = TriggerWorldBaker::poseFromObject(object);
    this->m_live.emplace(id, pose);

    TriggerTrack track;
    track.uniqueId = id;
    track.objectId = object->m_objectID;
    track.kind = kind;
    track.ready = false;
    TriggerWorldBaker::pushKey(track, 0, pose);
    auto [ins, ok] = this->m_tracks.emplace(id, std::move(track));
    (void)ok;
    return &ins->second;
}

std::vector<GameObject*> TriggerWorldBaker::objectsInGroup(int groupId) const {
    std::vector<GameObject*> out;
    if (groupId <= 0)
        return out;
    for (auto* object : this->m_objects) {
        if (!object)
            continue;
        if (!object->belongsToGroup(groupId))
            continue;
        out.push_back(object);
    }
    return out;
}

void TriggerWorldBaker::bakeOneEffect(EffectGameObject* effect) {
    if (!effect)
        return;

    TriggerKind const kind = TriggerClassify::classify(effect);
    // Follow-player / Dynamic* never enter static JSON (live probe only).
    if (TriggerClassify::isDynamic(kind)) {
        ++this->m_skippedDynamic;
        return;
    }
    if (!TriggerClassify::isStatic(kind))
        return;
    if (kind == TriggerKind::StaticSpawn)
        return;
    if (TriggerWorldBaker::isFollowLikeStatic(effect, kind)) {
        ++this->m_skippedFollow;
        return;
    }

    if (effect->m_isTouchTriggered) {
        ++this->m_skippedTouch;
        return;
    }

    int const groupId = effect->m_targetGroupID;
    if (groupId <= 0)
        return;

    auto targets = this->objectsInGroup(groupId);
    if (targets.empty())
        return;

    // Spawn-triggered: use spawn delay. Non-spawn (Commit 4): start-of-level t=0
    // — previously skipped all non-spawn → empty TW on many levels (e.g. id_80045794).
    bool const spawn = effect->m_isSpawnTriggered;
    int const startTick =
        spawn ? TriggerWorldBaker::durationTicks(effect->m_spawnTriggerDelay, this->m_tps) : 0;
    if (!spawn)
        ++this->m_bakedStartTimed;
    int const dur = TriggerWorldBaker::durationTicks(effect->m_duration, this->m_tps);

    if (kind == TriggerKind::StaticToggle) {
        for (auto* object : targets) {
            if (object == effect && targets.size() > 1)
                continue;
            auto* track = this->ensureTrack(object, TriggerKind::StaticToggle);
            if (!track)
                break;
            auto& pose = this->m_live[object->m_uniqueID];
            pose.active = effect->m_activateGroup;
            TriggerWorldBaker::pushKey(*track, startTick, pose);
            track->ready = track->keys.size() >= 1;
        }
        ++this->m_bakedEffects;
        return;
    }

    if (kind != TriggerKind::StaticMove && kind != TriggerKind::StaticRotate &&
        kind != TriggerKind::StaticScale) {
        return;
    }

    cocos2d::CCPoint center{0.f, 0.f};
    if (kind == TriggerKind::StaticRotate) {
        int const centerId = effect->m_centerGroupID;
        if (centerId > 0) {
            center = TriggerWorldBaker::groupCenter(centerId, this->m_objects, this->m_live);
        } else {
            for (auto* object : targets) {
                (void)this->ensureTrack(object, kind);
            }
            center = TriggerWorldBaker::groupCenter(groupId, this->m_objects, this->m_live);
        }
    }

    float scaleTargetX = 1.f;
    float scaleTargetY = 1.f;
    bool relativeScale = false;
    if (kind == TriggerKind::StaticScale) {
        auto* xf = geode::prelude::typeinfo_cast<TransformTriggerGameObject*>(effect);
        if (!xf)
            return;
        scaleTargetX = xf->m_objectScaleX;
        scaleTargetY = xf->m_objectScaleY;
        if (xf->m_divideX && std::fabs(scaleTargetX) > 1e-6f) {
            scaleTargetX = 1.f / scaleTargetX;
        }
        if (xf->m_divideY && std::fabs(scaleTargetY) > 1e-6f) {
            scaleTargetY = 1.f / scaleTargetY;
        }
        relativeScale = xf->m_relativeScale;
    }

    class TargetBake {
    public:
        GameObject* obj = nullptr;
        LivePose start{};
        LivePose end{};

    private:
    };
    std::vector<TargetBake> bakes;
    bakes.reserve(targets.size());

    for (auto* object : targets) {
        if (object == effect && targets.size() > 1)
            continue;
        auto* track = this->ensureTrack(object, kind);
        if (!track)
            break;
        TargetBake tb;
        tb.obj = object;
        tb.start = this->m_live[object->m_uniqueID];
        tb.end = tb.start;

        if (kind == TriggerKind::StaticMove) {
            tb.end.x += effect->m_moveOffset.x;
            tb.end.y += effect->m_moveOffset.y;
        } else if (kind == TriggerKind::StaticRotate) {
            float const deg =
                effect->m_rotationDegrees + 360.f * static_cast<float>(effect->m_times360);
            float const rad = deg * 0.017453292519943295f;
            float const candidate = std::cos(rad);
            float const state = std::sin(rad);
            float const deltaX = tb.start.x - center.x;
            float const deltaY = tb.start.y - center.y;
            tb.end.x = center.x + deltaX * candidate - deltaY * state;
            tb.end.y = center.y + deltaX * state + deltaY * candidate;
            if (!effect->m_lockObjectRotation) {
                tb.end.rotation = tb.start.rotation + deg;
            }
        } else if (kind == TriggerKind::StaticScale) {
            if (relativeScale) {
                tb.end.scaleX = tb.start.scaleX * scaleTargetX;
                tb.end.scaleY = tb.start.scaleY * scaleTargetY;
            } else {
                tb.end.scaleX = scaleTargetX;
                tb.end.scaleY = scaleTargetY;
            }
        }
        bakes.push_back(tb);
    }

    int const endTick = startTick + std::max(0, dur);
    int step = 1;
    if (dur > 0) {
        step = std::max(1, dur / 16);
    }

    for (auto& tb : bakes) {
        auto* track = this->ensureTrack(tb.obj, kind);
        if (!track)
            break;

        TriggerWorldBaker::pushKey(*track, startTick, tb.start);

        if (dur <= 0) {
            this->m_live[tb.obj->m_uniqueID] = tb.end;
            TriggerWorldBaker::pushKey(*track, startTick, tb.end);
            track->ready = true;
            continue;
        }

        for (int tick = startTick + step; tick < endTick; tick += step) {
            if (track->keys.size() >= TriggerWorldStore::kHardMaxKeysPerTrack)
                break;
            float const u = static_cast<float>(tick - startTick) / static_cast<float>(dur);
            LivePose mid = tb.start;
            mid.x = tb.start.x + (tb.end.x - tb.start.x) * u;
            mid.y = tb.start.y + (tb.end.y - tb.start.y) * u;
            mid.rotation = tb.start.rotation + (tb.end.rotation - tb.start.rotation) * u;
            mid.scaleX = tb.start.scaleX + (tb.end.scaleX - tb.start.scaleX) * u;
            mid.scaleY = tb.start.scaleY + (tb.end.scaleY - tb.start.scaleY) * u;
            mid.active = tb.end.active;
            TriggerWorldBaker::pushKey(*track, tick, mid);
        }

        this->m_live[tb.obj->m_uniqueID] = tb.end;
        TriggerWorldBaker::pushKey(*track, endTick, tb.end);
        track->ready = true;
    }
    ++this->m_bakedEffects;
}

void TriggerWorldBaker::tickSlice() {
    if (!this->m_running)
        return;
    if (this->m_cancel) {
        this->finishCancel();
        return;
    }

    PerfTrace::Scope const scope("triggerBake.slice");
    LagProbe::Scope const lagScope("eb.triggerBake.build");

    auto const wall0 = std::chrono::steady_clock::now();
    auto budgetHit = [&]() -> bool {
        auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - wall0)
                                             .count();
        return elapsedMicroseconds >= kBudgetUs;
    };

    while (!budgetHit()) {
        if (this->m_cancel) {
            this->finishCancel();
            return;
        }

        if (this->m_phase == Phase::WaitObjects) {
            auto* layer = this->m_layer;
            size_t children = 0;
            if (layer && layer->m_objectLayer) {
                if (auto* ch = layer->m_objectLayer->getChildren()) {
                    children = static_cast<size_t>(ch->count());
                }
            }
            bool const hasSections = layer && !layer->m_sections.empty();
            ++this->m_waitSlices;
            this->m_progress = 0.02f * std::min(1.f, static_cast<float>(this->m_waitSlices) /
                                                         static_cast<float>(kMaxWaitObjectSlices));
            if (children > 0 || hasSections) {
                this->m_childCount = children;
                this->m_childIndex = 0;
                this->m_objects.clear();
                this->m_effects.clear();
                this->m_seenIds.clear();
                if (children > 0) {
                    this->m_objects.reserve(children);
                    this->m_effects.reserve(256);
                }
                this->m_phase = Phase::Collect;
                AssistLog::debug(
                    fmt::format("TRIGGER-WORLD: objects ready ({} children) after {} wait slices",
                                children, this->m_waitSlices));
                continue;
            }
            if (this->m_waitSlices >= kMaxWaitObjectSlices) {
                this->m_collectIncomplete = true;
                this->m_phase = Phase::Commit;
                AssistLog::info(
                    "TRIGGER-WORLD: WaitObjects timed out — committing incomplete empty");
                continue;
            }
            break; // yield slice; try next physics tick
        }

        if (this->m_phase == Phase::Collect) {
            auto* layer = this->m_layer;
            if (!layer || !layer->m_objectLayer) {
                this->m_phase = Phase::CollectSections;
                this->m_secOuter = 0;
                this->m_secInner = 0;
                this->m_secSlot = 0;
                continue;
            }
            auto* children = layer->m_objectLayer->getChildren();
            if (!children) {
                this->m_phase = Phase::CollectSections;
                this->m_secOuter = 0;
                this->m_secInner = 0;
                this->m_secSlot = 0;
                continue;
            }

            // Collect must finish every child across slices (no skip on budget).
            size_t count = 0;
            while (this->m_childIndex < this->m_childCount && count < kCollectPerSlice &&
                   !budgetHit()) {
                auto* object = geode::prelude::typeinfo_cast<GameObject*>(
                    children->objectAtIndex(static_cast<unsigned>(this->m_childIndex)));
                ++this->m_childIndex;
                ++count;
                this->considerCollectedObject(object);
            }

            float const collectFrac = this->m_childCount > 0
                                          ? static_cast<float>(this->m_childIndex) /
                                                static_cast<float>(this->m_childCount)
                                          : 1.f;
            this->m_progress = 0.12f * collectFrac;

            if (this->m_childIndex >= this->m_childCount) {
                this->m_phase = Phase::CollectSections;
                this->m_secOuter = 0;
                this->m_secInner = 0;
                this->m_secSlot = 0;
            }
            continue;
        }

        if (this->m_phase == Phase::CollectSections) {
            auto* layer = this->m_layer;
            if (!layer) {
                this->m_phase = Phase::Bake;
                this->m_effectIndex = 0;
                continue;
            }

            auto& sections = layer->m_sections;
            size_t count = 0;
            while (count < kCollectPerSlice && !budgetHit()) {
                if (this->m_secOuter >= sections.size()) {
                    this->m_phase = Phase::Bake;
                    this->m_effectIndex = 0;
                    if (this->m_sectionExtras > 0) {
                        AssistLog::debug(fmt::format(
                            "TRIGGER-WORLD: section collect +{} objects (total objs={} effects={})",
                            this->m_sectionExtras, this->m_objects.size(), this->m_effects.size()));
                    }
                    break;
                }
                auto* mid = sections[this->m_secOuter];
                if (!mid || this->m_secInner >= mid->size()) {
                    ++this->m_secOuter;
                    this->m_secInner = 0;
                    this->m_secSlot = 0;
                    continue;
                }
                auto* inner = (*mid)[this->m_secInner];
                if (!inner || this->m_secSlot >= inner->size()) {
                    ++this->m_secInner;
                    this->m_secSlot = 0;
                    continue;
                }
                auto* rawData = (*inner)[this->m_secSlot];
                ++this->m_secSlot;
                ++count;
                size_t const before = this->m_seenIds.size();
                this->considerCollectedObject(geode::prelude::typeinfo_cast<GameObject*>(rawData));
                if (this->m_seenIds.size() > before) {
                    ++this->m_sectionExtras;
                }
            }

            this->m_progress =
                0.12f + 0.03f * (sections.empty()
                                     ? 1.f
                                     : std::min(1.f, static_cast<float>(this->m_secOuter) /
                                                         static_cast<float>(sections.size())));
            continue;
        }

        if (this->m_phase == Phase::Bake) {
            size_t count = 0;
            while (this->m_effectIndex < this->m_effects.size() && count < kEffectsPerSlice &&
                   !budgetHit()) {
                this->bakeOneEffect(this->m_effects[this->m_effectIndex]);
                ++this->m_effectIndex;
                ++count;
            }

            float const bakeFrac = this->m_effects.empty()
                                       ? 1.f
                                       : static_cast<float>(this->m_effectIndex) /
                                             static_cast<float>(this->m_effects.size());
            this->m_progress = 0.15f + 0.80f * bakeFrac;

            if (count > 0) {
                this->publishPartial();
            }

            if (this->m_effectIndex >= this->m_effects.size()) {
                this->m_phase = Phase::Commit;
            }
            continue;
        }

        if (this->m_phase == Phase::Commit) {
            this->publishPartial();
            this->m_phase = Phase::Idle;
            this->finishOk();
            return;
        }

        break;
    }
}
