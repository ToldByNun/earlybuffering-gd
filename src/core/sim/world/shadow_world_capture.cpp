#include "shadow_world_capture.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../trigger_world/trigger_classify.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../orbs/orb_stack_resolver.hpp"
#include "../orbs/toggle_orb_sim.hpp"
#include "../shadow_simulator.hpp"
#include <algorithm>
#include <vector>

void EbShadowWorldCapture::captureTriggerTransforms(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_savedTransforms.clear();
    if (!layer || !EbStageConfig::triggerBake())
        return;

    auto& store = EbTriggerWorldStore::get();
    EbLagProbe::Scope const scope("eb.trigger.capture");

    // Collect uniqueIds we may move this sim (ready static bake tracks).
    std::unordered_set<int> wanted;
    if (store.loaded() && store.hasTracks()) {
        wanted.reserve(store.trackCount());
        for (auto const& track : store.data().tracks) {
            if (!track.ready || track.keys.empty())
                continue;
            if (!EbTriggerClassify::isStatic(track.kind))
                continue;
            wanted.insert(track.uniqueId);
        }
    }
    if (wanted.empty())
        return;

    std::unordered_set<int> seen;
    seen.reserve(wanted.size());
    simulator.m_savedTransforms.reserve(std::min(wanted.size(), static_cast<size_t>(4096)));

    auto consider = [&](GameObject* object) {
        if (!object)
            return;
        int const uid = object->m_uniqueID;
        if (wanted.find(uid) == wanted.end())
            return;
        if (!seen.insert(uid).second)
            return;

        EbShadowSimulator::SavedTransform st;
        st.uniqueId = uid;
        st.obj = object;
        st.x = object->getPositionX();
        st.y = object->getPositionY();
        st.rotation = object->getRotation();
        st.scaleX = object->getScaleX();
        st.scaleY = object->getScaleY();
        st.nodeVisible = object->isVisible();
        st.dirty = false;
        simulator.m_savedTransforms.push_back(st);
    };

    // Primary: objectLayer children.
    if (auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr) {
        for (unsigned index = 0; index < children->count(); ++index) {
            consider(geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index)));
        }
    }
    // Secondary: section lists often hold objects children miss (same as TW baker).
    // Without this, bake at spawn can get tw=false → toggled spikes stay lethal.
    {
        auto& sections = layer->m_sections;
        for (auto* mid : sections) {
            if (!mid)
                continue;
            for (auto* inner : *mid) {
                if (!inner)
                    continue;
                for (auto* rawData : *inner) {
                    consider(geode::prelude::typeinfo_cast<GameObject*>(rawData));
                }
            }
        }
    }
}

void EbShadowWorldCapture::captureToggleUnknownTargets(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_toggleTargetIds.clear();
    if (!layer)
        return;

    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return;

    // Collect toggle-orb / touch-toggle effects with a target group.
    std::vector<EffectGameObject*> toggleEffects;
    toggleEffects.reserve(32);
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* effect =
            geode::prelude::typeinfo_cast<EffectGameObject*>(children->objectAtIndex(index));
        if (!effect)
            continue;
        if (effect->m_targetGroupID <= 0)
            continue;
        if (EbTriggerClassify::classify(effect) != TriggerKind::DynamicOther)
            continue;
        int const oid = effect->m_objectID;
        bool const toggleOrb = EbTriggerClassify::isToggleOrbId(oid);
        bool const touchToggle =
            EbTriggerClassify::isToggleTriggerId(oid) && effect->m_isTouchTriggered;
        if (!toggleOrb && !touchToggle)
            continue;
        toggleEffects.push_back(effect);
        if (toggleEffects.size() >= 256)
            break;
    }
    if (toggleEffects.empty())
        return;

    static constexpr size_t kMaxToggleTargets = 8192;
    for (auto* effect : toggleEffects) {
        int const groupId = effect->m_targetGroupID;
        if (groupId <= 0)
            continue;
        for (unsigned index = 0; index < children->count(); ++index) {
            if (simulator.m_toggleTargetIds.size() >= kMaxToggleTargets)
                return;
            auto* object =
                geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
            if (!object)
                continue;
            if (geode::prelude::typeinfo_cast<PlayerObject*>(object))
                continue;
            if (object == effect)
                continue;
            if (!object->belongsToGroup(groupId))
                continue;
            // Commit 7: jump/dash rings keep orb path — not Hard toggle-Unknown.
            if (EbOrbStackResolver::shouldSkipToggleFlip(object, effect))
                continue;
            simulator.m_toggleTargetIds.insert(object->m_uniqueID);
        }
    }
}

void EbShadowWorldCapture::captureDynamicFollows(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_dynamicTargetIds.clear();
    simulator.m_dynamicFollowJobs.clear();
    simulator.m_liveDynamicAppliedIds.clear();
    if (!layer || !EbStageConfig::triggerBake())
        return;

    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return;

    bool const wantJobs = !EbStageConfig::triggerBakeSkipDynamic();

    // First collect DynamicFollow effects only; skip the second full-roster scan when none exist.
    std::vector<EffectGameObject*> followEffects;
    for (unsigned index = 0; index < children->count(); ++index) {
        if (followEffects.size() >= EbShadowSimulator::kMaxDynamicFollowJobs)
            break;
        auto* effect =
            geode::prelude::typeinfo_cast<EffectGameObject*>(children->objectAtIndex(index));
        if (!effect)
            continue;
        if (effect->m_targetGroupID <= 0)
            continue;
        if (EbTriggerClassify::classify(effect) != TriggerKind::DynamicFollow)
            continue;
        followEffects.push_back(effect);
    }
    if (followEffects.empty())
        return;

    // Index solids once for group membership.
    std::vector<GameObject*> solids;
    solids.reserve(static_cast<size_t>(children->count()));
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!object)
            continue;
        if (geode::prelude::typeinfo_cast<PlayerObject*>(object))
            continue;
        solids.push_back(object);
    }

    std::unordered_set<int> savedTransformIds;
    if (wantJobs) {
        savedTransformIds.reserve(simulator.m_savedTransforms.size() + 64);
        for (auto const& saved : simulator.m_savedTransforms) {
            savedTransformIds.insert(saved.uniqueId);
        }
    }

    for (auto* effect : followEffects) {
        if (simulator.m_dynamicFollowJobs.size() >= EbShadowSimulator::kMaxDynamicFollowJobs)
            break;

        // Rotate-follow / non-positional: mark targets Unknown, no cheap probe.
        bool const rotateOnly = effect->m_rotateFollowP1 || effect->m_rotateFollowP2;
        bool const followY = EbTriggerClassify::isFollowPlayerTriggerId(effect->m_objectID);
        bool const targetPlayer = effect->m_targetPlayer1 || effect->m_targetPlayer2;
        bool const lockX = effect->m_lockToPlayerX || targetPlayer;
        bool const lockY = effect->m_lockToPlayerY || targetPlayer || followY;
        bool const probeable = !rotateOnly && (lockX || lockY);

        int const groupId = effect->m_targetGroupID;
        if (groupId <= 0)
            continue;

        EbShadowSimulator::DynamicFollowJob job;
        job.effectUniqueId = effect->m_uniqueID;
        job.effect = effect;
        job.lockX = lockX;
        job.lockY = lockY;
        job.followPlayerY = followY;
        job.offsetX = effect->m_moveOffset.x;
        job.offsetY =
            followY ? static_cast<float>(effect->m_followYOffset) : effect->m_moveOffset.y;

        for (auto* object : solids) {
            if (!object || !object->belongsToGroup(groupId))
                continue;
            if (object == effect)
                continue;
            simulator.m_dynamicTargetIds.insert(object->m_uniqueID);

            if (!wantJobs || !probeable)
                continue;
            if (job.targets.size() >= EbShadowSimulator::kMaxDynamicTargetsPerJob)
                break;

            EbShadowSimulator::SavedTransform st;
            st.uniqueId = object->m_uniqueID;
            st.obj = object;
            st.x = object->getPositionX();
            st.y = object->getPositionY();
            st.rotation = object->getRotation();
            st.scaleX = object->getScaleX();
            st.scaleY = object->getScaleY();
            st.nodeVisible = object->isVisible();
            st.dirty = false;
            job.targets.push_back(st);

            if (savedTransformIds.insert(st.uniqueId).second) {
                simulator.m_savedTransforms.push_back(st);
            }
        }

        if (wantJobs && probeable && !job.targets.empty()) {
            simulator.m_dynamicFollowJobs.push_back(std::move(job));
        }
    }
}

void EbShadowWorldCapture::resetTriggerTransforms(EbShadowSimulator& simulator) {
    if (simulator.m_savedTransforms.empty())
        return;

    for (auto const& st : simulator.m_savedTransforms) {
        if (!st.dirty)
            continue;
        st.dirty = false;
        auto* object = st.obj;
        if (!object || object->m_uniqueID != st.uniqueId)
            continue;

        object->setPosition({st.x, st.y});
        object->m_positionX = st.x;
        object->m_positionY = st.y;
        object->setRotation(st.rotation);
        object->setScaleX(st.scaleX);
        object->setScaleY(st.scaleY);
        object->setVisible(st.nodeVisible);
    }
}

void EbShadowWorldCapture::restoreTriggerTransforms(EbShadowSimulator& simulator,
                                                  GJBaseGameLayer* layer) {
    (void)layer;
    simulator.resetTriggerTransforms();
    simulator.m_savedTransforms.clear();
}

void EbShadowWorldCapture::beginCapture(EbShadowSimulator& simulator, PlayLayer* layer) {
    simulator.m_saved.clear();
    simulator.m_savedTransforms.clear();
    simulator.m_dynamicTargetIds.clear();
    simulator.m_toggleTargetIds.clear();
    simulator.m_dynamicFollowJobs.clear();
    simulator.m_liveDynamicAppliedIds.clear();
    simulator.m_toggleUnknownHit = false;
    simulator.m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;
    EbToggleOrbSim::get().restore();
    EbOrbStackResolver::get().begin();
    simulator.captureTriggerTransforms(layer);
    simulator.captureDynamicFollows(layer);
    simulator.captureToggleUnknownTargets(layer);
}

void EbShadowWorldCapture::resetCaptureForNextRun(EbShadowSimulator& simulator, PlayLayer* layer) {
    EbToggleOrbSim::get().restore();
    simulator.resetTriggerTransforms();
    simulator.m_triggerWorldAppliedThisTick = false;
    simulator.m_liveDynamicAppliedIds.clear();

    for (auto const& s : simulator.m_saved) {
        if (!s.obj)
            continue;
        s.obj->m_activated = s.activated;
        s.obj->m_activatedByPlayer1 = s.p1;
        s.obj->m_activatedByPlayer2 = s.p2;
        s.obj->m_poweredOn = s.poweredOn;
        s.obj->m_state = s.state;
        s.obj->m_isActivated = s.isActivated;
        s.obj->m_visible = s.egoVisible;
        s.obj->setOpacity(s.opacity);
        s.obj->setVisible(s.nodeVisible);
        if (s.isRing) {
            if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(s.obj)) {
                ring->m_claimTouch = s.claimTouch;
            }
        }
    }
    simulator.m_saved.clear();
    // Always restore — vanilla may mutate IDs without a SavedObject entry.
    if (layer) {
        layer->m_gameState.m_activatedObjectIDs = simulator.m_savedActivatedIDs;
    }
}

void EbShadowWorldCapture::restoreCapture(EbShadowSimulator& simulator, PlayLayer* layer) {
    EbToggleOrbSim::get().restore();
    {
        EbLagProbe::Scope const scope("eb.trigger.restore");
        simulator.restoreTriggerTransforms(layer);
    }
    simulator.m_dynamicFollowJobs.clear();
    simulator.m_dynamicTargetIds.clear();
    simulator.m_toggleTargetIds.clear();
    simulator.m_liveDynamicAppliedIds.clear();
    simulator.m_toggleUnknownHit = false;
    simulator.m_triggerWorldAppliedThisTick = false;

    for (auto const& s : simulator.m_saved) {
        if (!s.obj)
            continue;
        s.obj->m_activated = s.activated;
        s.obj->m_activatedByPlayer1 = s.p1;
        s.obj->m_activatedByPlayer2 = s.p2;
        s.obj->m_poweredOn = s.poweredOn;
        s.obj->m_state = s.state;
        s.obj->m_isActivated = s.isActivated;
        s.obj->m_visible = s.egoVisible;
        s.obj->setOpacity(s.opacity);
        s.obj->setVisible(s.nodeVisible);
        if (s.isRing) {
            if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(s.obj)) {
                ring->m_claimTouch = s.claimTouch;
            }
        }
    }
    simulator.m_saved.clear();
    // Always restore even when m_saved was empty (Problem C: live ring path).
    if (layer) {
        layer->m_gameState.m_activatedObjectIDs = simulator.m_savedActivatedIDs;
    }
}
