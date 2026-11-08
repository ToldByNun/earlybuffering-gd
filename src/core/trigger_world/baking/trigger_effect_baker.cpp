#include "trigger_effect_baker.hpp"
#include "../trigger_classify.hpp"
#include "../trigger_world_baker.hpp"
#include "../trigger_world_store.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <vector>

TriggerWorldBaker::LivePose TriggerEffectBaker::poseFromObject(GameObject* object) {
    TriggerWorldBaker::LivePose p;
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

TriggerPose TriggerEffectBaker::toKey(int tick, TriggerWorldBaker::LivePose const& live) {
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

void TriggerEffectBaker::pushKey(TriggerTrack& track, int tick,
                                 TriggerWorldBaker::LivePose const& live) {
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

int TriggerEffectBaker::durationTicks(float durationSec, double tps) {
    if (durationSec <= 0.f)
        return 0;
    int const ticks = static_cast<int>(std::lround(static_cast<double>(durationSec) * tps));
    return std::max(0, ticks);
}

bool TriggerEffectBaker::isFollowLikeStatic(EffectGameObject* effect, TriggerKind kind) {
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

cocos2d::CCPoint
TriggerEffectBaker::groupCenter(int groupId, std::vector<GameObject*> const& objects,
                                std::unordered_map<int, TriggerWorldBaker::LivePose> const& live) {
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

TriggerTrack* TriggerEffectBaker::ensureTrack(TriggerWorldBaker& owner, GameObject* object,
                                              TriggerKind kind) {
    if (!object)
        return nullptr;
    int const id = object->m_uniqueID;
    auto it = owner.m_tracks.find(id);
    if (it != owner.m_tracks.end())
        return &it->second;
    if (owner.m_tracks.size() >= TriggerWorldStore::kSoftMaxTracks) {
        TriggerWorldStore::get().noteSoftcap("bake-tracks");
        return nullptr;
    }

    TriggerWorldBaker::LivePose const pose = TriggerWorldBaker::poseFromObject(object);
    owner.m_live.emplace(id, pose);

    TriggerTrack track;
    track.uniqueId = id;
    track.objectId = object->m_objectID;
    track.kind = kind;
    track.ready = false;
    TriggerWorldBaker::pushKey(track, 0, pose);
    auto [ins, ok] = owner.m_tracks.emplace(id, std::move(track));
    (void)ok;
    return &ins->second;
}

std::vector<GameObject*> TriggerEffectBaker::objectsInGroup(TriggerWorldBaker const& owner,
                                                            int groupId) {
    std::vector<GameObject*> out;
    if (groupId <= 0)
        return out;
    for (auto* object : owner.m_objects) {
        if (!object)
            continue;
        if (!object->belongsToGroup(groupId))
            continue;
        out.push_back(object);
    }
    return out;
}

void TriggerEffectBaker::bakeOneEffect(TriggerWorldBaker& owner, EffectGameObject* effect) {
    if (!effect)
        return;

    TriggerKind const kind = TriggerClassify::classify(effect);
    // Follow-player / Dynamic* never enter static JSON (live probe only).
    if (TriggerClassify::isDynamic(kind)) {
        ++owner.m_skippedDynamic;
        return;
    }
    if (!TriggerClassify::isStatic(kind))
        return;
    if (kind == TriggerKind::StaticSpawn)
        return;
    if (TriggerWorldBaker::isFollowLikeStatic(effect, kind)) {
        ++owner.m_skippedFollow;
        return;
    }

    if (effect->m_isTouchTriggered) {
        ++owner.m_skippedTouch;
        return;
    }

    int const groupId = effect->m_targetGroupID;
    if (groupId <= 0)
        return;

    auto targets = owner.objectsInGroup(groupId);
    if (targets.empty())
        return;

    // Spawn-triggered: use spawn delay. Non-spawn (Commit 4): start-of-level t=0
    // — previously skipped all non-spawn → empty TW on many levels (e.g. id_80045794).
    bool const spawn = effect->m_isSpawnTriggered;
    int const startTick =
        spawn ? TriggerWorldBaker::durationTicks(effect->m_spawnTriggerDelay, owner.m_tps) : 0;
    if (!spawn)
        ++owner.m_bakedStartTimed;
    int const dur = TriggerWorldBaker::durationTicks(effect->m_duration, owner.m_tps);

    if (kind == TriggerKind::StaticToggle) {
        for (auto* object : targets) {
            if (object == effect && targets.size() > 1)
                continue;
            auto* track = owner.ensureTrack(object, TriggerKind::StaticToggle);
            if (!track)
                break;
            auto& pose = owner.m_live[object->m_uniqueID];
            pose.active = effect->m_activateGroup;
            TriggerWorldBaker::pushKey(*track, startTick, pose);
            track->ready = track->keys.size() >= 1;
        }
        ++owner.m_bakedEffects;
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
            center = TriggerWorldBaker::groupCenter(centerId, owner.m_objects, owner.m_live);
        } else {
            for (auto* object : targets) {
                (void)owner.ensureTrack(object, kind);
            }
            center = TriggerWorldBaker::groupCenter(groupId, owner.m_objects, owner.m_live);
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
        TriggerWorldBaker::LivePose start{};
        TriggerWorldBaker::LivePose end{};

    private:
    };
    std::vector<TargetBake> bakes;
    bakes.reserve(targets.size());

    for (auto* object : targets) {
        if (object == effect && targets.size() > 1)
            continue;
        auto* track = owner.ensureTrack(object, kind);
        if (!track)
            break;
        TargetBake tb;
        tb.obj = object;
        tb.start = owner.m_live[object->m_uniqueID];
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
        auto* track = owner.ensureTrack(tb.obj, kind);
        if (!track)
            break;

        TriggerWorldBaker::pushKey(*track, startTick, tb.start);

        if (dur <= 0) {
            owner.m_live[tb.obj->m_uniqueID] = tb.end;
            TriggerWorldBaker::pushKey(*track, startTick, tb.end);
            track->ready = true;
            continue;
        }

        for (int tick = startTick + step; tick < endTick; tick += step) {
            if (track->keys.size() >= TriggerWorldStore::kHardMaxKeysPerTrack)
                break;
            float const u = static_cast<float>(tick - startTick) / static_cast<float>(dur);
            TriggerWorldBaker::LivePose mid = tb.start;
            mid.x = tb.start.x + (tb.end.x - tb.start.x) * u;
            mid.y = tb.start.y + (tb.end.y - tb.start.y) * u;
            mid.rotation = tb.start.rotation + (tb.end.rotation - tb.start.rotation) * u;
            mid.scaleX = tb.start.scaleX + (tb.end.scaleX - tb.start.scaleX) * u;
            mid.scaleY = tb.start.scaleY + (tb.end.scaleY - tb.start.scaleY) * u;
            mid.active = tb.end.active;
            TriggerWorldBaker::pushKey(*track, tick, mid);
        }

        owner.m_live[tb.obj->m_uniqueID] = tb.end;
        TriggerWorldBaker::pushKey(*track, endTick, tb.end);
        track->ready = true;
    }
    ++owner.m_bakedEffects;
}
