#include "TriggerWorldBaker.hpp"
#include "AssistController.hpp"
#include "LevelProfile.hpp"
#include "TriggerClassify.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/LagProbe.hpp"
#include "../debug/PerfTrace.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

TriggerWorldBaker& TriggerWorldBaker::get() {
    static TriggerWorldBaker inst;
    return inst;
}

TriggerWorldBaker::LivePose TriggerWorldBaker::poseFromObject(GameObject* obj) {
    LivePose p;
    if (!obj) return p;
    p.x = obj->getPositionX();
    p.y = obj->getPositionY();
    p.rotation = obj->getRotation();
    p.scaleX = obj->getScaleX();
    p.scaleY = obj->getScaleY();
    p.active = obj->isVisible();
    p.objectId = obj->m_objectID;
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
    if (track.keys.size() >= TriggerWorldStore::kMaxKeysPerTrack) return;
    if (!track.keys.empty() && track.keys.back().tick == tick) {
        track.keys.back() = TriggerWorldBaker::toKey(tick, live);
        return;
    }
    if (!track.keys.empty() && track.keys.back().tick > tick) return;
    track.keys.push_back(TriggerWorldBaker::toKey(tick, live));
}

int TriggerWorldBaker::durationTicks(float durationSec, double tps) {
    if (durationSec <= 0.f) return 0;
    int const ticks = static_cast<int>(std::lround(static_cast<double>(durationSec) * tps));
    return std::max(0, ticks);
}

bool TriggerWorldBaker::isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind) {
    if (!effect) return false;
    if (kind != TriggerKind::StaticMove) return false;
    if (TriggerClassify::isFollowTriggerId(effect->m_objectID)) return true;
    if (effect->m_useMoveTarget || effect->m_followCPP) return true;
    if (effect->m_isDynamicMode || effect->m_isDirectionFollowSnap360) return true;
    return false;
}

cocos2d::CCPoint TriggerWorldBaker::groupCenter(
    int groupId,
    std::vector<GameObject*> const& objects,
    std::unordered_map<int, LivePose> const& live
) {
    double sx = 0.0;
    double sy = 0.0;
    int n = 0;
    for (auto* obj : objects) {
        if (!obj || groupId <= 0) continue;
        if (!obj->belongsToGroup(groupId)) continue;
        auto it = live.find(obj->m_uniqueID);
        if (it == live.end()) continue;
        sx += it->second.x;
        sy += it->second.y;
        ++n;
    }
    if (n <= 0) return { 0.f, 0.f };
    return {
        static_cast<float>(sx / static_cast<double>(n)),
        static_cast<float>(sy / static_cast<double>(n))
    };
}

void TriggerWorldBaker::clearJob() {
    this->m_layer = nullptr;
    this->m_phase = Phase::Idle;
    this->m_childIndex = 0;
    this->m_childCount = 0;
    this->m_effectIndex = 0;
    this->m_objects.clear();
    this->m_effects.clear();
    this->m_live.clear();
    this->m_tracks.clear();
    this->m_bakedEffects = 0;
    this->m_skippedFollow = 0;
    this->m_skippedDynamic = 0;
    this->m_skippedNonSpawn = 0;
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
    this->m_status = TriggerWorldStore::get().statusLine();
    this->m_lastBakedEffects = this->m_bakedEffects;
    this->m_lastSkippedFollow = this->m_skippedFollow;
    this->m_lastSkippedDynamic = this->m_skippedDynamic;
    this->m_lastSkippedNonSpawn = this->m_skippedNonSpawn;
    this->m_lastChildCount = this->m_childCount;
    this->m_lastEffectCount = this->m_effects.size();
    AssistLog::info(fmt::format(
        "TRIGGER-WORLD: baked {} effects → {} tracks ({} ready) @ {:.0f} tps "
        "(skip follow={} dynamic={} non-spawn={} children={} collectedEffects={})",
        this->m_bakedEffects,
        TriggerWorldStore::get().trackCount(),
        TriggerWorldStore::get().readyTrackCount(),
        this->m_tps,
        this->m_skippedFollow,
        this->m_skippedDynamic,
        this->m_skippedNonSpawn,
        this->m_lastChildCount,
        this->m_lastEffectCount
    ));
    this->clearJob();
}

std::string TriggerWorldBaker::statusLine() const {
    if (this->m_running) {
        return fmt::format(
            "trigger-world: building {:.0f}%",
            this->m_progress * 100.f
        );
    }
    if (!this->m_status.empty()) return this->m_status;
    return TriggerWorldStore::get().statusLine();
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
        AssistLog::debug(fmt::format(
            "TRIGGER-WORLD: cache hit ({})",
            this->m_status
        ));
        return;
    }

    this->start(layer);
}

void TriggerWorldBaker::resumeIfEmpty(PlayLayer* layer) {
    if (!layer || !StageConfig::triggerBake()) return;
    if (this->m_running) return;
    auto& store = TriggerWorldStore::get();
    if (!store.loaded() || store.hasTracks()) return;
    AssistLog::debug("TRIGGER-WORLD: resume build after empty cancel");
    this->start(layer);
}

bool TriggerWorldBaker::start(PlayLayer* layer) {
    if (!layer) return false;
    if (!StageConfig::triggerBake()) return false;
    if (this->m_running) return false;

    this->clearJob();
    this->m_cancel = false;
    this->m_running = true;
    this->m_progress = 0.f;
    this->m_status = "trigger-world: building";
    this->m_layer = layer;
    this->m_phase = Phase::Collect;

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

    AssistLog::debug(fmt::format(
        "TRIGGER-WORLD: bake queued ({} children)",
        this->m_childCount
    ));
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
        if (track.keys.empty()) continue;
        TriggerTrack copy = track;
        std::sort(
            copy.keys.begin(),
            copy.keys.end(),
            [](TriggerPose const& a, TriggerPose const& b) {
                return a.tick < b.tick;
            }
        );
        data.tracks.push_back(std::move(copy));
    }
    TriggerWorldStore::get().setData(std::move(data));
}

TriggerTrack* TriggerWorldBaker::ensureTrack(GameObject* obj, TriggerKind kind) {
    if (!obj) return nullptr;
    int const id = obj->m_uniqueID;
    auto it = this->m_tracks.find(id);
    if (it != this->m_tracks.end()) return &it->second;
    if (this->m_tracks.size() >= TriggerWorldStore::kMaxTracks) return nullptr;

    LivePose const pose = TriggerWorldBaker::poseFromObject(obj);
    this->m_live.emplace(id, pose);

    TriggerTrack track;
    track.uniqueId = id;
    track.objectId = obj->m_objectID;
    track.kind = kind;
    track.ready = false;
    TriggerWorldBaker::pushKey(track, 0, pose);
    auto [ins, ok] = this->m_tracks.emplace(id, std::move(track));
    (void)ok;
    return &ins->second;
}

std::vector<GameObject*> TriggerWorldBaker::objectsInGroup(int groupId) const {
    std::vector<GameObject*> out;
    if (groupId <= 0) return out;
    for (auto* obj : this->m_objects) {
        if (!obj) continue;
        if (!obj->belongsToGroup(groupId)) continue;
        out.push_back(obj);
    }
    return out;
}

void TriggerWorldBaker::bakeOneEffect(EffectGameObject* effect) {
    if (!effect) return;

        TriggerKind const kind = TriggerClassify::classify(effect);
        // Follow-player / Dynamic* never enter static JSON (live probe only).
        if (TriggerClassify::isDynamic(kind)) {
            ++this->m_skippedDynamic;
            return;
        }
    if (!TriggerClassify::isStatic(kind)) return;
    if (kind == TriggerKind::StaticSpawn) return;
    if (TriggerWorldBaker::isFollowLikeStatic(effect, kind)) {
        ++this->m_skippedFollow;
        return;
    }

    if (effect->m_isTouchTriggered) return;
    if (!effect->m_isSpawnTriggered) {
        ++this->m_skippedNonSpawn;
        return;
    }

    int const groupId = effect->m_targetGroupID;
    if (groupId <= 0) return;

    auto targets = this->objectsInGroup(groupId);
    if (targets.empty()) return;

    int const startTick = TriggerWorldBaker::durationTicks(
        effect->m_spawnTriggerDelay, this->m_tps
    );
    int const dur = TriggerWorldBaker::durationTicks(effect->m_duration, this->m_tps);

    if (kind == TriggerKind::StaticToggle) {
        for (auto* obj : targets) {
            if (obj == effect && targets.size() > 1) continue;
            auto* track = this->ensureTrack(obj, TriggerKind::StaticToggle);
            if (!track) break;
            auto& pose = this->m_live[obj->m_uniqueID];
            pose.active = effect->m_activateGroup;
            TriggerWorldBaker::pushKey(*track, startTick, pose);
            track->ready = track->keys.size() >= 1;
        }
        ++this->m_bakedEffects;
        return;
    }

    if (kind != TriggerKind::StaticMove
        && kind != TriggerKind::StaticRotate
        && kind != TriggerKind::StaticScale) {
        return;
    }

    cocos2d::CCPoint center{ 0.f, 0.f };
    if (kind == TriggerKind::StaticRotate) {
        int const centerId = effect->m_centerGroupID;
        if (centerId > 0) {
            center = TriggerWorldBaker::groupCenter(
                centerId, this->m_objects, this->m_live
            );
        }
        else {
            for (auto* obj : targets) {
                (void)this->ensureTrack(obj, kind);
            }
            center = TriggerWorldBaker::groupCenter(
                groupId, this->m_objects, this->m_live
            );
        }
    }

    float scaleTargetX = 1.f;
    float scaleTargetY = 1.f;
    bool relativeScale = false;
    if (kind == TriggerKind::StaticScale) {
        auto* xf = typeinfo_cast<TransformTriggerGameObject*>(effect);
        if (!xf) return;
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

    struct TargetBake {
        GameObject* obj = nullptr;
        LivePose start{};
        LivePose end{};
    };
    std::vector<TargetBake> bakes;
    bakes.reserve(targets.size());

    for (auto* obj : targets) {
        if (obj == effect && targets.size() > 1) continue;
        auto* track = this->ensureTrack(obj, kind);
        if (!track) break;
        TargetBake tb;
        tb.obj = obj;
        tb.start = this->m_live[obj->m_uniqueID];
        tb.end = tb.start;

        if (kind == TriggerKind::StaticMove) {
            tb.end.x += effect->m_moveOffset.x;
            tb.end.y += effect->m_moveOffset.y;
        }
        else if (kind == TriggerKind::StaticRotate) {
            float const deg = effect->m_rotationDegrees
                + 360.f * static_cast<float>(effect->m_times360);
            float const rad = deg * 0.017453292519943295f;
            float const c = std::cos(rad);
            float const s = std::sin(rad);
            float const dx = tb.start.x - center.x;
            float const dy = tb.start.y - center.y;
            tb.end.x = center.x + dx * c - dy * s;
            tb.end.y = center.y + dx * s + dy * c;
            if (!effect->m_lockObjectRotation) {
                tb.end.rotation = tb.start.rotation + deg;
            }
        }
        else if (kind == TriggerKind::StaticScale) {
            if (relativeScale) {
                tb.end.scaleX = tb.start.scaleX * scaleTargetX;
                tb.end.scaleY = tb.start.scaleY * scaleTargetY;
            }
            else {
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
        if (!track) break;

        TriggerWorldBaker::pushKey(*track, startTick, tb.start);

        if (dur <= 0) {
            this->m_live[tb.obj->m_uniqueID] = tb.end;
            TriggerWorldBaker::pushKey(*track, startTick, tb.end);
            track->ready = true;
            continue;
        }

        for (int t = startTick + step; t < endTick; t += step) {
            if (track->keys.size() >= TriggerWorldStore::kMaxKeysPerTrack) break;
            float const u = static_cast<float>(t - startTick)
                / static_cast<float>(dur);
            LivePose mid = tb.start;
            mid.x = tb.start.x + (tb.end.x - tb.start.x) * u;
            mid.y = tb.start.y + (tb.end.y - tb.start.y) * u;
            mid.rotation = tb.start.rotation
                + (tb.end.rotation - tb.start.rotation) * u;
            mid.scaleX = tb.start.scaleX
                + (tb.end.scaleX - tb.start.scaleX) * u;
            mid.scaleY = tb.start.scaleY
                + (tb.end.scaleY - tb.start.scaleY) * u;
            mid.active = tb.end.active;
            TriggerWorldBaker::pushKey(*track, t, mid);
        }

        this->m_live[tb.obj->m_uniqueID] = tb.end;
        TriggerWorldBaker::pushKey(*track, endTick, tb.end);
        track->ready = true;
    }
    ++this->m_bakedEffects;
}

void TriggerWorldBaker::tickSlice() {
    if (!this->m_running) return;
    if (this->m_cancel) {
        this->finishCancel();
        return;
    }

    PerfTrace::Scope const scope("triggerBake.slice");
    LagProbe::Scope const lagScope("eb.triggerBake.build");

    auto const wall0 = std::chrono::steady_clock::now();
    auto budgetHit = [&]() -> bool {
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall0
        ).count();
        return us >= kBudgetUs;
    };

    while (!budgetHit()) {
        if (this->m_cancel) {
            this->finishCancel();
            return;
        }

        if (this->m_phase == Phase::Collect) {
            auto* layer = this->m_layer;
            if (!layer || !layer->m_objectLayer) {
                this->m_phase = Phase::Bake;
                continue;
            }
            auto* children = layer->m_objectLayer->getChildren();
            if (!children) {
                this->m_phase = Phase::Bake;
                continue;
            }

            size_t n = 0;
            while (this->m_childIndex < this->m_childCount
                && n < kCollectPerSlice
                && !budgetHit()) {
                auto* obj = typeinfo_cast<GameObject*>(
                    children->objectAtIndex(static_cast<unsigned>(this->m_childIndex))
                );
                ++this->m_childIndex;
                ++n;
                if (!obj) continue;
                if (typeinfo_cast<PlayerObject*>(obj)) continue;
                this->m_objects.push_back(obj);
                if (auto* effect = typeinfo_cast<EffectGameObject*>(obj)) {
                    this->m_effects.push_back(effect);
                }
            }

            float const collectFrac = this->m_childCount > 0
                ? static_cast<float>(this->m_childIndex)
                    / static_cast<float>(this->m_childCount)
                : 1.f;
            this->m_progress = 0.15f * collectFrac;

            if (this->m_childIndex >= this->m_childCount) {
                this->m_phase = Phase::Bake;
                this->m_effectIndex = 0;
            }
            continue;
        }

        if (this->m_phase == Phase::Bake) {
            size_t n = 0;
            while (this->m_effectIndex < this->m_effects.size()
                && n < kEffectsPerSlice
                && !budgetHit()) {
                this->bakeOneEffect(this->m_effects[this->m_effectIndex]);
                ++this->m_effectIndex;
                ++n;
            }

            float const bakeFrac = this->m_effects.empty()
                ? 1.f
                : static_cast<float>(this->m_effectIndex)
                    / static_cast<float>(this->m_effects.size());
            this->m_progress = 0.15f + 0.80f * bakeFrac;

            if (n > 0) {
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
