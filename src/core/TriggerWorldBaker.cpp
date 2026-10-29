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
#include <cmath>
#include <unordered_map>
#include <vector>

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

void TriggerWorldBaker::cancel() {
    this->m_cancel = true;
    if (!this->m_running) {
        this->m_progress = 0.f;
        this->m_status.clear();
    }
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

void TriggerWorldBaker::tickSlice() {
    // Commit 7: slice work across frames. Commit 6 finishes inside start().
}

bool TriggerWorldBaker::start(PlayLayer* layer) {
    if (!layer) return false;
    if (!StageConfig::triggerBake()) return false;
    if (this->m_running) return false;

    this->m_cancel = false;
    this->m_running = true;
    this->m_progress = 0.f;
    this->m_status = "trigger-world: building";

    {
        PerfTrace::Scope const scope("triggerBake.build");
        LagProbe::Scope const lagScope("eb.triggerBake.build");
        this->buildSync(layer);
    }

    this->m_running = false;
    if (this->m_cancel) {
        this->m_status = "trigger-world: cancelled";
        this->m_progress = 0.f;
        AssistLog::debug("TRIGGER-WORLD: bake cancelled");
        return false;
    }
    this->m_status = TriggerWorldStore::get().statusLine();
    return true;
}

void TriggerWorldBaker::buildSync(PlayLayer* layer) {
    if (!layer || !layer->m_objectLayer) return;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return;

    double tps = AssistController::get().estimatedTps();
    if (tps < 60.0 || tps > 480.0) tps = 240.0;

    std::string levelKey;
    if (layer->m_level) {
        levelKey = LevelProfile::makeKey(layer->m_level);
    }
    if (levelKey.empty()) {
        levelKey = TriggerWorldStore::get().data().levelKey;
    }

    std::vector<GameObject*> objects;
    objects.reserve(static_cast<size_t>(children->count()));
    for (unsigned i = 0; i < children->count(); ++i) {
        if (this->cancelled()) return;
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!obj) continue;
        if (typeinfo_cast<PlayerObject*>(obj)) continue;
        objects.push_back(obj);
    }

    std::vector<EffectGameObject*> effects;
    effects.reserve(256);
    for (auto* obj : objects) {
        auto* effect = typeinfo_cast<EffectGameObject*>(obj);
        if (!effect) continue;
        effects.push_back(effect);
    }

    std::unordered_map<int, LivePose> live;
    std::unordered_map<int, TriggerTrack> tracks;

    auto ensureTrack = [&](GameObject* obj, TriggerKind kind) -> TriggerTrack* {
        if (!obj) return nullptr;
        int const id = obj->m_uniqueID;
        auto it = tracks.find(id);
        if (it != tracks.end()) return &it->second;
        if (tracks.size() >= TriggerWorldStore::kMaxTracks) return nullptr;

        LivePose const pose = TriggerWorldBaker::poseFromObject(obj);
        live.emplace(id, pose);

        TriggerTrack track;
        track.uniqueId = id;
        track.objectId = obj->m_objectID;
        track.kind = kind;
        track.ready = false;
        TriggerWorldBaker::pushKey(track, 0, pose);
        auto [ins, ok] = tracks.emplace(id, std::move(track));
        (void)ok;
        return &ins->second;
    };

    auto objectsInGroup = [&](int groupId) {
        std::vector<GameObject*> out;
        if (groupId <= 0) return out;
        for (auto* obj : objects) {
            if (!obj) continue;
            if (!obj->belongsToGroup(groupId)) continue;
            out.push_back(obj);
        }
        return out;
    };

    size_t bakedEffects = 0;
    size_t skippedFollow = 0;
    size_t skippedDynamic = 0;
    size_t skippedNonSpawn = 0;

    size_t const total = effects.size();
    size_t index = 0;
    for (auto* effect : effects) {
        if (this->cancelled()) return;
        ++index;
        this->m_progress = total > 0
            ? static_cast<float>(index) / static_cast<float>(total)
            : 1.f;

        TriggerKind const kind = TriggerClassify::classify(effect);
        if (TriggerClassify::isDynamic(kind)) {
            ++skippedDynamic;
            continue;
        }
        if (!TriggerClassify::isStatic(kind)) continue;
        if (kind == TriggerKind::StaticSpawn) continue;
        if (TriggerWorldBaker::isFollowLikeStatic(effect, kind)) {
            ++skippedFollow;
            continue;
        }

        // v1: only spawn-timed statics (absolute timeline from attempt start).
        // Touch → DynamicOther already; enter-X auto timing needs player path.
        if (effect->m_isTouchTriggered) continue;
        if (!effect->m_isSpawnTriggered) {
            ++skippedNonSpawn;
            continue;
        }

        int const groupId = effect->m_targetGroupID;
        if (groupId <= 0) continue;

        auto targets = objectsInGroup(groupId);
        if (targets.empty()) continue;

        // Spawn delay on the effect itself (often 0 on Move; Spawn uses its own).
        int const startTick = TriggerWorldBaker::durationTicks(effect->m_spawnTriggerDelay, tps);
        int const dur = TriggerWorldBaker::durationTicks(effect->m_duration, tps);

        if (kind == TriggerKind::StaticToggle) {
            for (auto* obj : targets) {
                if (this->cancelled()) return;
                // Don't key the toggle trigger onto itself unless it's the only member.
                if (obj == effect && targets.size() > 1) continue;
                auto* track = ensureTrack(obj, TriggerKind::StaticToggle);
                if (!track) break;
                auto& pose = live[obj->m_uniqueID];
                pose.active = effect->m_activateGroup;
                TriggerWorldBaker::pushKey(*track, startTick, pose);
                track->ready = track->keys.size() >= 1;
            }
            ++bakedEffects;
            continue;
        }

        if (kind != TriggerKind::StaticMove
            && kind != TriggerKind::StaticRotate
            && kind != TriggerKind::StaticScale) {
            continue;
        }

        cocos2d::CCPoint center{ 0.f, 0.f };
        if (kind == TriggerKind::StaticRotate) {
            int const centerId = effect->m_centerGroupID;
            if (centerId > 0) {
                center = TriggerWorldBaker::groupCenter(centerId, objects, live);
            }
            else {
                // Fallback: centroid of the moving group before this effect.
                for (auto* obj : targets) {
                    auto* track = ensureTrack(obj, kind);
                    (void)track;
                }
                center = TriggerWorldBaker::groupCenter(groupId, objects, live);
            }
        }

        float scaleTargetX = 1.f;
        float scaleTargetY = 1.f;
        bool relativeScale = false;
        if (kind == TriggerKind::StaticScale) {
            auto* xf = typeinfo_cast<TransformTriggerGameObject*>(effect);
            if (!xf) continue;
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
            auto* track = ensureTrack(obj, kind);
            if (!track) break;
            TargetBake tb;
            tb.obj = obj;
            tb.start = live[obj->m_uniqueID];
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
            if (this->cancelled()) return;
            auto* track = ensureTrack(tb.obj, kind);
            if (!track) break;

            TriggerWorldBaker::pushKey(*track, startTick, tb.start);

            if (dur <= 0) {
                live[tb.obj->m_uniqueID] = tb.end;
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

            live[tb.obj->m_uniqueID] = tb.end;
            TriggerWorldBaker::pushKey(*track, endTick, tb.end);
            track->ready = true;
        }
        ++bakedEffects;
    }

    if (this->cancelled()) return;

    TriggerWorldData data;
    data.levelKey = levelKey;
    data.tps = tps;
    data.version = TriggerWorldStore::kFileVersion;
    data.tracks.reserve(tracks.size());
    for (auto& [id, track] : tracks) {
        (void)id;
        if (track.keys.empty()) continue;
        // Sort keys ascending (effects may overlap oddly).
        std::sort(
            track.keys.begin(),
            track.keys.end(),
            [](TriggerPose const& a, TriggerPose const& b) {
                return a.tick < b.tick;
            }
        );
        data.tracks.push_back(std::move(track));
    }

    TriggerWorldStore::get().setData(std::move(data));
    this->m_progress = 1.f;

    AssistLog::info(fmt::format(
        "TRIGGER-WORLD: baked {} effects → {} tracks ({} ready) @ {:.0f} tps "
        "(skip follow={} dynamic={} non-spawn={})",
        bakedEffects,
        TriggerWorldStore::get().trackCount(),
        TriggerWorldStore::get().readyTrackCount(),
        tps,
        skippedFollow,
        skippedDynamic,
        skippedNonSpawn
    ));
}
