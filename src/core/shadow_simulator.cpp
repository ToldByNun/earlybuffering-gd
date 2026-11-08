#include "shadow_simulator.hpp"
#include "../config/stage_config.hpp"
#include "../debug/assist_log.hpp"
#include "../debug/lag_probe.hpp"
#include "../debug/perf_trace.hpp"
#include "../dev/dev_config.hpp"
#include "../dev/dev_reject_ledger.hpp"
#include "assist_clock.hpp"
#include "assist_horizon.hpp"
#include "assist_pick_v1.hpp"
#include "assist_pick_v2.hpp"
#include "bake_map.hpp"
#include "long_foresight.hpp"
#include "miss_assist_contracts.hpp"
#include "mode_survival.hpp"
#include "opportunity_probe.hpp"
#include "orb_stack_resolver.hpp"
#include "perfect_assist_policy.hpp"
#include "prefix_sim.hpp"
#include "sim_collision_policy.hpp"
#include "sim_session_guard.hpp"
#include "sim_trust.hpp"
#include "toggle_orb_sim.hpp"
#include "travel_foresight.hpp"
#include "trigger_world_store.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// Post-pick hold probe: how long can the player hold, and when to let go safely.

// budget: at most this many extra runOnes

ShadowSimulator& ShadowSimulator::get() {
    static ShadowSimulator inst;
    return inst;
}

void ShadowSimulator::attach(PlayLayer* layer) {
    detach();
    if (!layer || !layer->m_objectLayer)
        return;

    auto makeHidden = [&](char const* id) -> PlayerObject* {
        auto* player = PlayerObject::create(1, 1, layer, layer, true);
        if (!player)
            return nullptr;
        player->setPosition({0.f, 105.f});
        player->setID(id);
        // objectLayer owns the node (addChild retain). Do NOT extra-retain —
        // release-on-quit while still parented / double-free in ~PlayerObject
        // during setNextScene was the quit AV (CCObject::release).
        layer->m_objectLayer->addChild(player);
        hideGhost(player);
        return player;
    };

    m_shadow = makeHidden("earlybuffer-shadow-player"_spr);
    for (int index = 0; index < SNAPSHOT_SLOTS; ++index) {
        auto const id = fmt::format("earlybuffer-snapshot-{}", index);
        m_snapshots[index] = makeHidden(id.c_str());
    }
    for (int index = 0; index < DEV_EVAL_SLOTS; ++index) {
        auto const id = fmt::format("earlybuffer-eval-{}", index);
        m_evalGhosts[index] = makeHidden(id.c_str());
    }
    m_evalActive = nullptr;
    m_evalAbort = false;
    m_nextSnapshot = SNAPSHOT_RING_BEGIN;
    m_hostLayer = layer;
}

void ShadowSimulator::detach(bool layerDying) {
    // Never leave the sim flag set — quit can interrupt a traj rebuild mid-capture.
    this->m_simulating = false;
    this->m_bakeOffline = false;
    this->m_applyingRing = false;
    this->m_orbScanning = false;
    this->m_orbOverlapTick = false;
    this->m_dead = false;
    this->m_jumped = false;
    this->m_unknown = false;
    this->m_toggleUnknownHit = false;
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_deathObject = nullptr;
    this->m_simAbsTick = -1;
    this->m_simTickBase = 0;
    this->m_poseRemapDt = -1.f;
    this->m_triggerWorldAppliedThisTick = false;
    this->m_dynamicTargetIds.clear();
    this->m_toggleTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    ToggleOrbSim::get().restore();
    this->m_saved.clear();
    this->m_savedTransforms.clear();
    // gd::map: `= {}` is ambiguous on libstdc++/Android — use clear().
    this->m_savedActivatedIDs.clear();
    // Clear host first so a nested evacuate (if any) sees "not attached".
    this->m_hostLayer = nullptr;

    auto sanitize = [](PlayerObject* player) {
        if (!player)
            return;
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        if (player->m_touchingRings) {
            player->m_touchingRings->removeAllObjects();
        }
        if (player->m_waveTrail) {
            player->m_waveTrail->reset();
        }
        player->setVisible(false);
    };

    auto kill = [&](PlayerObject*& player) {
        if (!player)
            return;
        sanitize(player);
        if (layerDying) {
            // Parent tree is already tearing down — do not removeFromParent /
            // release (double-free in ~PlayerObject during setNextScene).
            player = nullptr;
            return;
        }
        // Normal quit / reattach: destroy while PlayLayer is still alive.
        if (player->getParent()) {
            player->removeFromParentAndCleanup(true);
        }
        player = nullptr;
    };
    kill(this->m_shadow);
    for (auto& s : this->m_snapshots)
        kill(s);
    for (auto& e : this->m_evalGhosts)
        kill(e);
    this->m_evalActive = nullptr;
    this->m_evalAbort = false;
}

bool ShadowSimulator::isOwnedGhost(PlayerObject* player) const {
    if (!player)
        return false;
    if (player == m_shadow)
        return true;
    for (auto* shadowPlayer : m_snapshots) {
        if (player == shadowPlayer)
            return true;
    }
    for (auto* entry : m_evalGhosts) {
        if (player == entry)
            return true;
    }
    return false;
}

void ShadowSimulator::abortEval() {
    this->m_evalAbort = true;
}

bool ShadowSimulator::tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object) {
    // Never call GJBaseGameLayer::removeObjectFromSection — it AVs when indices
    // are stale, sections are half-built (init/setupHasCompleted), or mx is bad
    // (crash read @ 0xB8). Erase our pointer from the inner vector ourselves.
    if (!layer || !object)
        return false;
    int const objectX = object->m_outerSectionIndex;
    int const ix = object->m_innerSectionIndex;
    if (objectX < 0 || ix < 0) {
        object->m_outerSectionIndex = -1;
        object->m_innerSectionIndex = -1;
        object->m_middleSectionIndex = -1;
        return false;
    }

    auto& sections = layer->m_sections;
    bool erased = false;
    if (objectX < static_cast<int>(sections.size())) {
        auto* mid = sections[static_cast<size_t>(objectX)];
        if (mid && ix < static_cast<int>(mid->size())) {
            auto* inner = (*mid)[static_cast<size_t>(ix)];
            if (inner) {
                for (int index = static_cast<int>(inner->size()) - 1; index >= 0; --index) {
                    if ((*inner)[static_cast<size_t>(index)] != object)
                        continue;
                    inner->erase(inner->begin() + static_cast<std::ptrdiff_t>(index));
                    erased = true;
                    break;
                }
            }
        }
    }

    object->m_outerSectionIndex = -1;
    object->m_innerSectionIndex = -1;
    object->m_middleSectionIndex = -1;
    return erased;
}

void ShadowSimulator::scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, ShadowSimulator* self) {
    if (!layer || !self)
        return;

    // Only match our known ghost pointers by raw address. Section slots can hold
    // freed/poison entries (0xFFF…); typeinfo_cast on those AVs (read @ -1).
    GameObject* ghosts[1 + SNAPSHOT_SLOTS + DEV_EVAL_SLOTS]{};
    int ghostCount = 0;
    if (self->m_shadow) {
        ghosts[ghostCount++] = self->m_shadow;
    }
    for (auto* slot : self->m_snapshots) {
        if (!slot)
            continue;
        ghosts[ghostCount++] = slot;
    }
    for (auto* effectObject : self->m_evalGhosts) {
        if (!effectObject)
            continue;
        ghosts[ghostCount++] = effectObject;
    }
    if (ghostCount <= 0)
        return;

    auto isOwnedSlot = [&](GameObject* object) -> bool {
        if (!object)
            return false;
        auto const addr = reinterpret_cast<uintptr_t>(object);
        // Reject null-page and classic poison (-1) without dereferencing.
        if (addr < 0x10000ull || addr == ~uintptr_t{0})
            return false;
        for (int gameObject = 0; gameObject < ghostCount; ++gameObject) {
            if (ghosts[gameObject] == object)
                return true;
        }
        return false;
    };

    auto scrubCell = [&](auto* inner, int& found) {
        if (!inner || found >= ghostCount)
            return;
        for (int index = static_cast<int>(inner->size()) - 1; index >= 0; --index) {
            if (static_cast<size_t>(index) >= inner->size())
                continue;
            auto* object = (*inner)[static_cast<size_t>(index)];
            if (!isOwnedSlot(object))
                continue;
            inner->erase(inner->begin() + static_cast<std::ptrdiff_t>(index));
            ++found;
            if (found >= ghostCount)
                return;
        }
    };

    auto& sections = layer->m_sections;
    int found = 0;

    // Fast path: only touch cells named by each ghost's section indices.
    for (int gameObject = 0; gameObject < ghostCount; ++gameObject) {
        auto* object = ghosts[gameObject];
        if (!object)
            continue;
        int const objectX = object->m_outerSectionIndex;
        int const ix = object->m_innerSectionIndex;
        int const mx = object->m_middleSectionIndex;
        if (objectX < 0 || ix < 0 || mx < 0)
            continue;
        if (objectX >= static_cast<int>(sections.size()))
            continue;
        auto* mid = sections[static_cast<size_t>(objectX)];
        if (!mid || ix >= static_cast<int>(mid->size()))
            continue;
        scrubCell((*mid)[static_cast<size_t>(ix)], found);
        if (found >= ghostCount)
            return;
    }

    // Slow path only if some ghosts still might linger (bad / -1 indices).
    // Early-exit once every owned pointer was erased — never walk the whole
    // grid "just in case" after that (was ~20ms/restart on large levels).
    if (found >= ghostCount)
        return;

    size_t const outerN = sections.size();
    for (size_t objectX = 0; objectX < outerN; ++objectX) {
        auto* mid = sections[objectX];
        if (!mid)
            continue;
        size_t const midN = mid->size();
        for (size_t ix = 0; ix < midN; ++ix) {
            scrubCell((*mid)[ix], found);
            if (found >= ghostCount)
                return;
        }
    }
}

void ShadowSimulator::parkGhosts() {
    auto scrub = [&](PlayerObject* player) {
        if (!player)
            return;
        if (auto* layer = PlayLayer::get()) {
            if (layer == this->m_hostLayer) {
                ShadowSimulator::tryRemoveFromSection(layer, player);
            }
        }
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        hideGhost(player);
    };
    auto parkOffLevel = [&](PlayerObject* player) {
        if (!player)
            return;
        scrub(player);
        player->setPosition({0.f, 105.f});
        player->m_position = cocos2d::CCPoint{0.f, 105.f};
        player->m_yVelocity = 0.0;
        player->m_isDead = false;
        // Wave trail points stay in world space after a sim — clear so leftover
        // streak geometry cannot interact with live collision/debug paths.
        if (player->m_waveTrail) {
            player->m_waveTrail->reset();
        }
    };

    parkOffLevel(this->m_shadow);
    for (auto* effectObject : this->m_evalGhosts) {
        parkOffLevel(effectObject);
    }
    // Snapshots must keep captured pose for bake / outcome verify / traj
    // samplePath — scrub+hide only so they leave section vectors.
    for (auto* slot : this->m_snapshots) {
        scrub(slot);
    }
}

void ShadowSimulator::beginSession(PlayLayer* layer) {
    this->m_simulating = true;
    this->m_applyingRing = false;
    this->beginCapture(layer);
}

void ShadowSimulator::endSession(PlayLayer* layer) {
    this->m_simAbsTick = -1;
    this->m_poseRemapDt = -1.f;
    this->m_bakeOffline = false;
    if (layer) {
        this->restoreCapture(layer);
    } else {
        // Still drop claim deltas / flags if the layer vanished mid-sim.
        ToggleOrbSim::get().restore();
        this->m_saved.clear();
        this->m_savedTransforms.clear();
        this->m_dynamicTargetIds.clear();
        this->m_toggleTargetIds.clear();
        this->m_dynamicFollowJobs.clear();
        this->m_liveDynamicAppliedIds.clear();
        this->m_toggleUnknownHit = false;
        this->m_triggerWorldAppliedThisTick = false;
    }
    this->m_simulating = false;
    this->m_applyingRing = false;
    this->parkGhosts();
}

gd::vector<GameObject*>& ShadowSimulator::collisionFilterScratch() {
    return this->m_collisionFilterBuf;
}

void ShadowSimulator::evacuateGhosts(GJBaseGameLayer* layer) {
    PerfTrace::Scope const scope("evacuateGhosts");
    LagProbe::Scope const lagScope("eb.evacuateGhosts");
    // Abort any in-flight sim; do NOT restoreCapture here — reset/checkpoint
    // rebuilds object state and restoring into freed/reused objects can AV.
    this->m_simulating = false;
    this->m_applyingRing = false;
    this->m_orbScanning = false;
    this->m_orbOverlapTick = false;
    this->m_dead = false;
    this->m_jumped = false;
    this->m_unknown = false;
    this->m_toggleUnknownHit = false;
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_deathObject = nullptr;
    this->m_simAbsTick = -1;
    this->m_simTickBase = 0;
    this->m_poseRemapDt = -1.f;
    this->m_triggerWorldAppliedThisTick = false;
    this->m_dynamicTargetIds.clear();
    this->m_toggleTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    ToggleOrbSim::get().restore();
    // If a sim aborted after applyAtTick moved solids, put them back before
    // we drop the snapshot — reset/checkpoint must not inherit drifted poses.
    if (!this->m_savedTransforms.empty() && layer) {
        LagProbe::Scope const state("eb.trigger.restore");
        this->restoreTriggerTransforms(layer);
    }
    this->m_saved.clear();
    this->m_savedTransforms.clear();
    this->m_savedActivatedIDs.clear();

    // PlayLayer::init → setupHasCompleted → resetLevel runs BEFORE attach().
    // Touching section vectors or stale ghosts there AVs (GD read @ 0xB8).
    if (!layer || layer != this->m_hostLayer || !this->m_shadow) {
        LagProbe::get().mark("evacuate.skip", "no host / no shadow / wrong layer");
        return;
    }

    // Scrub WHILE indices are still valid. tryRemove clears them to -1 — calling
    // scrub after that forced the slow full-grid walk (~10–20ms on large levels)
    // on every death→reset after assist had simulated.
    bool anyIndexed = false;
    auto noteIndexed = [&](PlayerObject* player) {
        if (!player)
            return;
        if (player->m_outerSectionIndex >= 0 || player->m_innerSectionIndex >= 0 ||
            player->m_middleSectionIndex >= 0) {
            anyIndexed = true;
        }
    };
    noteIndexed(this->m_shadow);
    for (auto* slot : this->m_snapshots) {
        noteIndexed(slot);
    }
    for (auto* effectObject : this->m_evalGhosts) {
        noteIndexed(effectObject);
    }
    LagProbe::get().mark("evacuate.indexed", anyIndexed ? "yes" : "no");
    if (anyIndexed) {
        LagProbe::Scope const state("eb.evacuate.scrub");
        ShadowSimulator::scrubOwnedGhostsFromSections(layer, this);
    }
    // Belt-and-suspenders: index erase if scrub missed a stale cell.
    {
        LagProbe::Scope const state("eb.evacuate.tryRemove");
        ShadowSimulator::tryRemoveFromSection(layer, this->m_shadow);
        for (auto* slot : this->m_snapshots) {
            ShadowSimulator::tryRemoveFromSection(layer, slot);
        }
        for (auto* effectObject : this->m_evalGhosts) {
            ShadowSimulator::tryRemoveFromSection(layer, effectObject);
        }
    }

    auto evac = [&](PlayerObject* player) {
        if (!player)
            return;
        player->m_outerSectionIndex = -1;
        player->m_innerSectionIndex = -1;
        player->m_middleSectionIndex = -1;
        player->setPosition({0.f, 105.f});
        player->m_position = cocos2d::CCPoint{0.f, 105.f};
        player->m_yVelocity = 0.0;
        player->m_isDead = false;
        if (player->m_waveTrail) {
            player->m_waveTrail->reset();
        }
        if (player->m_touchingRings) {
            player->m_touchingRings->removeAllObjects();
        }
        if (player->m_collisionLogTop) {
            this->clearCollisionLogs(player);
        }
        this->hideGhost(player);
    };

    {
        LagProbe::Scope const state("eb.evacuate.park");
        evac(this->m_shadow);
        for (auto* slot : this->m_snapshots) {
            evac(slot);
        }
        for (auto* effectObject : this->m_evalGhosts) {
            evac(effectObject);
        }
    }
    this->m_evalActive = nullptr;
    // Cancel in-flight eval loop; next evalDelayFromFreeze clears the flag.
    this->m_evalAbort = true;
}

void ShadowSimulator::markDead(PlayerObject* player, GameObject* object) {
    // Active sim body (live shadow or eval ghost) drives m_dead.
    if (player == m_shadow || (m_evalActive && player == m_evalActive)) {
        m_dead = true;
        m_deathObject = object;
    }
}

void ShadowSimulator::noteJump() {
    m_jumped = true;
}

void ShadowSimulator::hideGhost(PlayerObject* player) {
    if (!player)
        return;
    player->setVisible(false);
    player->setOpacity(0);
    player->m_playEffects = false;
    if (player->m_waveTrail) {
        player->m_waveTrail->setVisible(false);
        player->m_waveTrail->setOpacity(0);
    }
    // Shadow particle emitters must not paint the real level during sim.
    auto mute = [](cocos2d::CCParticleSystemQuad* queryOffset) {
        if (!queryOffset)
            return;
        queryOffset->setVisible(false);
        queryOffset->stopSystem();
        queryOffset->setEmissionRate(0.f);
    };
    mute(player->m_playerGroundParticles);
    mute(player->m_trailingParticles);
    mute(player->m_shipClickParticles);
    mute(player->m_vehicleGroundParticles);
    mute(player->m_ufoClickParticles);
    mute(player->m_robotBurstParticles);
    mute(player->m_dashParticles);
    mute(player->m_swingBurstParticles1);
    mute(player->m_swingBurstParticles2);
    mute(player->m_landParticles0);
    mute(player->m_landParticles1);
}

void ShadowSimulator::noteActivated(GameObject* gobj) {
    auto* object = geode::prelude::typeinfo_cast<EnhancedGameObject*>(gobj);
    if (!object)
        return;
    for (auto const& s : m_saved) {
        if (s.obj == object)
            return;
    }
    SavedObject s;
    s.obj = object;
    s.activated = object->m_activated;
    s.p1 = object->m_activatedByPlayer1;
    s.p2 = object->m_activatedByPlayer2;
    s.poweredOn = object->m_poweredOn;
    s.state = object->m_state;
    s.isActivated = object->m_isActivated;
    s.egoVisible = object->m_visible;
    s.opacity = object->getOpacity();
    s.nodeVisible = object->isVisible();
    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object)) {
        s.isRing = true;
        s.claimTouch = ring->m_claimTouch;
    }
    m_saved.push_back(s);
}

void ShadowSimulator::noteOrbOverlap() {
    m_orbOverlapTick = true;
}

void ShadowSimulator::applyRingJump(PlayerObject* shadow, RingObject* ring) {
    if (!shadow || !ring || (shadow != m_shadow && shadow != m_evalActive))
        return;

    // Real GD: orbs only fire while Jump is held. Without this, wait-ticks before a
    // delayed press claim the orb early and every orb prediction is wrong.
    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    bool const holding = shadow->m_holdingButtons[jumpIdx];
    if (!holding) {
        m_orbOverlapTick = true;
        return;
    }

    // Already used this run (and not multi-activate) → one jump only.
    bool const multi = ring->m_isMultiActivate || ring->canAllowMultiActivate();
    if (!multi && (ring->m_claimTouch || ring->m_activated))
        return;
    if (!multi && shadow->m_touchedRings.count(ring->m_uniqueID))
        return;

    noteActivated(ring);
    m_activatedRing = true;
    ++m_ringCount;

    // Real orb physics. Re-entrancy flag lets the ringJump hook call the original.
    // VFX hooks (spawnCircle, activateObject visuals, particles) stay blocked.
    // RAII so a throw / early exit never leaves m_applyingRing stuck (iOS live path).
    class ApplyingRingGuard {
    public:
        bool& flag;
        explicit ApplyingRingGuard(bool& f) : flag(f) {
            flag = true;
        }
        ~ApplyingRingGuard() {
            flag = false;
        }

    private:
    };
    ApplyingRingGuard applyingGuard(m_applyingRing);
    shadow->ringJump(ring, false);

    // Claim for the rest of this run so collision doesn't re-boost every tick.
    // restoreCapture() puts the real orb back afterward.
    if (!multi) {
        ring->m_claimTouch = true;
        ring->m_activated = true;
        ring->m_isActivated = true;
        if (shadow->m_isSecondPlayer)
            ring->m_activatedByPlayer2 = true;
        else
            ring->m_activatedByPlayer1 = true;
        shadow->m_touchedRings.insert(ring->m_uniqueID);
    }
    shadow->m_touchedRing = true;
    shadow->m_hasEverHitRing = true;
}

void ShadowSimulator::captureTriggerTransforms(PlayLayer* layer) {
    this->m_savedTransforms.clear();
    if (!layer || !StageConfig::triggerBake())
        return;

    auto& store = TriggerWorldStore::get();
    LagProbe::Scope const scope("eb.trigger.capture");

    // Collect uniqueIds we may move this sim (ready static bake tracks).
    std::unordered_set<int> wanted;
    if (store.loaded() && store.hasTracks()) {
        wanted.reserve(store.trackCount());
        for (auto const& track : store.data().tracks) {
            if (!track.ready || track.keys.empty())
                continue;
            if (!TriggerClassify::isStatic(track.kind))
                continue;
            wanted.insert(track.uniqueId);
        }
    }
    if (wanted.empty())
        return;

    std::unordered_set<int> seen;
    seen.reserve(wanted.size());
    this->m_savedTransforms.reserve(std::min(wanted.size(), static_cast<size_t>(4096)));

    auto consider = [&](GameObject* object) {
        if (!object)
            return;
        int const uid = object->m_uniqueID;
        if (wanted.find(uid) == wanted.end())
            return;
        if (!seen.insert(uid).second)
            return;

        SavedTransform st;
        st.uniqueId = uid;
        st.obj = object;
        st.x = object->getPositionX();
        st.y = object->getPositionY();
        st.rotation = object->getRotation();
        st.scaleX = object->getScaleX();
        st.scaleY = object->getScaleY();
        st.nodeVisible = object->isVisible();
        st.dirty = false;
        this->m_savedTransforms.push_back(st);
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

bool ShadowSimulator::isDynamicTriggerTarget(int uniqueId) const {
    return this->m_dynamicTargetIds.find(uniqueId) != this->m_dynamicTargetIds.end();
}

bool ShadowSimulator::isToggleTriggerTarget(int uniqueId) const {
    return this->m_toggleTargetIds.find(uniqueId) != this->m_toggleTargetIds.end();
}

bool ShadowSimulator::liveDynamicApplied(int uniqueId) const {
    return this->m_liveDynamicAppliedIds.find(uniqueId) != this->m_liveDynamicAppliedIds.end();
}

void ShadowSimulator::noteUnknown(GameObject* object) {
    this->m_unknown = true;
    if (object && this->isToggleTriggerTarget(object->m_uniqueID)) {
        this->m_toggleUnknownHit = true;
    }
    if (!this->m_collectUnknownHist || !object)
        return;
    bool const hard = SimCollisionPolicy::unknownClass(object) == SimUnknownClass::Hard;
    int const oid = object->m_objectID;
    auto& bin = this->m_unknownHistMap[oid];
    if (bin.count == 0) {
        bin.objectId = oid;
        bin.objectType = static_cast<int>(object->m_objectType);
        bin.hard = hard;
    }
    // Prefer hard if any overlap of this objectId was hard.
    if (hard)
        bin.hard = true;
    bin.uniqueId = object->m_uniqueID;
    ++bin.count;
}

void ShadowSimulator::captureToggleUnknownTargets(PlayLayer* layer) {
    this->m_toggleTargetIds.clear();
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
        if (TriggerClassify::classify(effect) != TriggerKind::DynamicOther)
            continue;
        int const oid = effect->m_objectID;
        bool const toggleOrb = TriggerClassify::isToggleOrbId(oid);
        bool const touchToggle =
            TriggerClassify::isToggleTriggerId(oid) && effect->m_isTouchTriggered;
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
            if (this->m_toggleTargetIds.size() >= kMaxToggleTargets)
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
            if (OrbStackResolver::shouldSkipToggleFlip(object, effect))
                continue;
            this->m_toggleTargetIds.insert(object->m_uniqueID);
        }
    }
}

void ShadowSimulator::captureDynamicFollows(PlayLayer* layer) {
    this->m_dynamicTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    if (!layer || !StageConfig::triggerBake())
        return;

    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return;

    bool const wantJobs = !StageConfig::triggerBakeSkipDynamic();

    // First collect DynamicFollow effects only; skip the second full-roster scan when none exist.
    std::vector<EffectGameObject*> followEffects;
    for (unsigned index = 0; index < children->count(); ++index) {
        if (followEffects.size() >= kMaxDynamicFollowJobs)
            break;
        auto* effect =
            geode::prelude::typeinfo_cast<EffectGameObject*>(children->objectAtIndex(index));
        if (!effect)
            continue;
        if (effect->m_targetGroupID <= 0)
            continue;
        if (TriggerClassify::classify(effect) != TriggerKind::DynamicFollow)
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
        savedTransformIds.reserve(this->m_savedTransforms.size() + 64);
        for (auto const& saved : this->m_savedTransforms) {
            savedTransformIds.insert(saved.uniqueId);
        }
    }

    for (auto* effect : followEffects) {
        if (this->m_dynamicFollowJobs.size() >= kMaxDynamicFollowJobs)
            break;

        // Rotate-follow / non-positional: mark targets Unknown, no cheap probe.
        bool const rotateOnly = effect->m_rotateFollowP1 || effect->m_rotateFollowP2;
        bool const followY = TriggerClassify::isFollowPlayerTriggerId(effect->m_objectID);
        bool const targetPlayer = effect->m_targetPlayer1 || effect->m_targetPlayer2;
        bool const lockX = effect->m_lockToPlayerX || targetPlayer;
        bool const lockY = effect->m_lockToPlayerY || targetPlayer || followY;
        bool const probeable = !rotateOnly && (lockX || lockY);

        int const groupId = effect->m_targetGroupID;
        if (groupId <= 0)
            continue;

        DynamicFollowJob job;
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
            this->m_dynamicTargetIds.insert(object->m_uniqueID);

            if (!wantJobs || !probeable)
                continue;
            if (job.targets.size() >= kMaxDynamicTargetsPerJob)
                break;

            SavedTransform st;
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
                this->m_savedTransforms.push_back(st);
            }
        }

        if (wantJobs && probeable && !job.targets.empty()) {
            this->m_dynamicFollowJobs.push_back(std::move(job));
        }
    }
}

void ShadowSimulator::applyLiveDynamicNear(PlayLayer* layer, PlayerObject* ghost) {
    this->m_liveDynamicAppliedIds.clear();
    if (!layer || !ghost)
        return;
    if (!StageConfig::triggerBake())
        return;
    if (StageConfig::triggerBakeSkipDynamic())
        return;
    // Dual follow is ambiguous (P1 vs P2) — leave Unknown.
    if (layer->m_gameState.m_isDualMode && layer->m_player2)
        return;
    if (this->m_dynamicFollowJobs.empty())
        return;

    float const gx = ghost->getPositionX();
    float const gy = ghost->getPositionY();
    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;

    for (auto& job : this->m_dynamicFollowJobs) {
        if (applied >= kMaxApplyPerTick)
            break;
        auto* effect = job.effect;
        if (!effect || effect->m_uniqueID != job.effectUniqueId)
            continue;

        for (auto const& t : job.targets) {
            if (applied >= kMaxApplyPerTick)
                break;
            auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                                 std::chrono::steady_clock::now() - wall0)
                                                 .count();
            if (elapsedMicroseconds >= kApplyBudgetUs)
                break;

            auto* object = t.obj;
            if (!object || object->m_uniqueID != t.uniqueId)
                continue;
            // Prefer targets near the ghost; far follow targets stay Unknown.
            if (std::fabs(object->getPositionX() - gx) > kApplyMarginX)
                continue;

            float x = t.x;
            float y = t.y;
            if (job.followPlayerY || job.lockY) {
                y = gy + job.offsetY;
            }
            if (job.lockX) {
                x = gx + job.offsetX;
            }

            TriggerPose pose{};
            pose.tick = this->m_simAbsTick >= 0 ? this->m_simAbsTick : 0;
            pose.x = x;
            pose.y = y;
            pose.rotation = t.rotation;
            pose.scaleX = t.scaleX;
            pose.scaleY = t.scaleY;
            pose.active = t.nodeVisible;
            TriggerWorldStore::writePose(object, pose);
            for (auto const& st : this->m_savedTransforms) {
                if (st.uniqueId == t.uniqueId) {
                    st.dirty = true;
                    break;
                }
            }
            this->m_liveDynamicAppliedIds.insert(t.uniqueId);
            ++applied;
        }
    }
}

void ShadowSimulator::resetTriggerTransforms() {
    if (this->m_savedTransforms.empty())
        return;

    for (auto const& st : this->m_savedTransforms) {
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

void ShadowSimulator::restoreTriggerTransforms(GJBaseGameLayer* layer) {
    (void)layer;
    this->resetTriggerTransforms();
    this->m_savedTransforms.clear();
}

void ShadowSimulator::beginCapture(PlayLayer* layer) {
    this->m_saved.clear();
    this->m_savedTransforms.clear();
    this->m_dynamicTargetIds.clear();
    this->m_toggleTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    this->m_toggleUnknownHit = false;
    this->m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;
    ToggleOrbSim::get().restore();
    OrbStackResolver::get().begin();
    this->captureTriggerTransforms(layer);
    this->captureDynamicFollows(layer);
    this->captureToggleUnknownTargets(layer);
}

void ShadowSimulator::resetCaptureForNextRun(PlayLayer* layer) {
    ToggleOrbSim::get().restore();
    this->resetTriggerTransforms();
    this->m_triggerWorldAppliedThisTick = false;
    this->m_liveDynamicAppliedIds.clear();

    for (auto const& s : this->m_saved) {
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
    this->m_saved.clear();
    // Always restore — vanilla may mutate IDs without a SavedObject entry.
    if (layer) {
        layer->m_gameState.m_activatedObjectIDs = this->m_savedActivatedIDs;
    }
}

void ShadowSimulator::restoreCapture(PlayLayer* layer) {
    ToggleOrbSim::get().restore();
    {
        LagProbe::Scope const scope("eb.trigger.restore");
        this->restoreTriggerTransforms(layer);
    }
    this->m_dynamicFollowJobs.clear();
    this->m_dynamicTargetIds.clear();
    this->m_toggleTargetIds.clear();
    this->m_liveDynamicAppliedIds.clear();
    this->m_toggleUnknownHit = false;
    this->m_triggerWorldAppliedThisTick = false;

    for (auto const& s : this->m_saved) {
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
    this->m_saved.clear();
    // Always restore even when m_saved was empty (Problem C: live ring path).
    if (layer) {
        layer->m_gameState.m_activatedObjectIDs = this->m_savedActivatedIDs;
    }
}

void ShadowSimulator::copyWaveTrail(PlayerObject* destination, PlayerObject* source) {
    auto* delay = destination->m_waveTrail;
    auto* state = source->m_waveTrail;
    if (!delay || !state)
        return;
    delay->reset();
    delay->m_currentPoint = state->m_currentPoint;
    delay->m_waveSize = state->m_waveSize;
    delay->m_pulseSize = state->m_pulseSize;
    delay->m_isSolid = state->m_isSolid;
    delay->m_isFlipped = state->m_isFlipped;
    delay->m_drawStreak = state->m_drawStreak;
    if (!state->m_pointArray)
        return;
    for (unsigned index = 0; index < state->m_pointArray->count(); ++index) {
        if (auto* node = static_cast<PointNode*>(state->m_pointArray->objectAtIndex(index))) {
            delay->addPoint(node->m_point);
        }
    }
}

void ShadowSimulator::clearCollisionLogs(PlayerObject* state) {
    state->m_collisionLogTop->removeAllObjects();
    state->m_collisionLogBottom->removeAllObjects();
    state->m_collisionLogLeft->removeAllObjects();
    state->m_collisionLogRight->removeAllObjects();
    state->m_lastCollisionTop = -1;
    state->m_lastCollisionBottom = -1;
    state->m_lastCollisionLeft = -1;
    state->m_lastCollisionRight = -1;
}

void ShadowSimulator::applyTriggerWorldAtTick(PlayLayer* layer, int absTick, PlayerObject* ghost) {
    this->m_triggerWorldAppliedThisTick = false;
    if (!layer || absTick < 0)
        return;
    if (!StageConfig::triggerBake())
        return;
    if (this->m_savedTransforms.empty())
        return;

    auto& store = TriggerWorldStore::get();
    if (!store.loaded() || !store.hasTracks())
        return;

    LagProbe::Scope const lagScope("eb.trigger.apply");

    float const gx = ghost ? ghost->getPositionX() : 0.f;
    class SimulationCandidate {
    public:
        SavedTransform const* st = nullptr;
        float dist = 0.f;
        // Named isNear — Windows headers define `near` as a macro.
        bool isNear = false;

    private:
    };
    std::vector<SimulationCandidate> cands;
    cands.reserve(128);
    constexpr float kCullMarginX = kApplyMarginX * 2.5f;
    for (auto const& st : this->m_savedTransforms) {
        auto* object = st.obj;
        if (!object || object->m_uniqueID != st.uniqueId)
            continue;
        float const distNow = std::fabs(object->getPositionX() - gx);
        float const distBase = std::fabs(st.x - gx);
        float const dist = std::min(distNow, distBase);
        if (dist > kCullMarginX)
            continue;
        SimulationCandidate c;
        c.st = &st;
        c.dist = dist;
        c.isNear = dist <= kApplyMarginX;
        cands.push_back(c);
    }
    // Near objects first, then closer far objects — skip remainder if budget/cap.
    std::sort(cands.begin(), cands.end(),
              [](SimulationCandidate const& a, SimulationCandidate const& b) {
                  if (a.isNear != b.isNear)
                      return a.isNear && !b.isNear;
                  return a.dist < b.dist;
              });

    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;
    for (auto const& c : cands) {
        if (applied >= kMaxApplyPerTick)
            break;
        auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now() - wall0)
                                             .count();
        if (elapsedMicroseconds >= kApplyBudgetUs)
            break;

        auto* object = c.st->obj;
        TriggerPose pose{};
        bool ok = false;
        if (this->m_poseRemapDt > 0.f) {
            ok = store.samplePose(c.st->uniqueId, absTick, this->m_poseRemapDt, pose);
        } else {
            ok = store.samplePose(c.st->uniqueId, absTick, pose);
        }
        if (!ok)
            continue;
        TriggerWorldStore::writePose(object, pose);
        c.st->dirty = true;
        ++applied;
    }
    // Far / over-budget tracks hold last written pose (or capture baseline).
    this->m_triggerWorldAppliedThisTick = true;
}

void ShadowSimulator::stepPreCollide(PlayLayer* layer, PlayerObject* state, float deltaTime) {
    // Always use the full step dt here — tiny CBF remainders launch slopes
    // and also under-detect orb/spike overlaps.
    this->m_triggerWorldAppliedThisTick = false;
    this->m_liveDynamicAppliedIds.clear();
    if (this->m_simAbsTick >= 0) {
        this->applyTriggerWorldAtTick(layer, this->m_simAbsTick, state);
        this->applyLiveDynamicNear(layer, state);
    }
    float const gdDt = AssistClock::secondsToGdStep(deltaTime);
    hideGhost(state);
    clearCollisionLogs(state);
    layer->checkCollisions(state, gdDt, false);
    hideGhost(state);
}

void ShadowSimulator::stepPhysics(PlayLayer* layer, PlayerObject* state, float stepDt,
                                  float deltaTime) {
    this->m_triggerWorldAppliedThisTick = false;
    this->m_liveDynamicAppliedIds.clear();
    if (this->m_simAbsTick >= 0) {
        this->applyTriggerWorldAtTick(layer, this->m_simAbsTick, state);
        this->applyLiveDynamicNear(layer, state);
    }
    // v1.2.1 contract: ghost must see the same GD step units as live
    // PlayerObject::update (0.25 @ 240 TPS). stepDt/dt are AssistClock seconds.
    float const gdStepDt = AssistClock::secondsToGdStep(stepDt);
    float colDt = stepDt;
    if (colDt < deltaTime)
        colDt = deltaTime; // same rule as pre-collide: never a tiny remainder
    float const gdColDt = AssistClock::secondsToGdStep(colDt);
    hideGhost(state);
    state->update(gdStepDt);
    hideGhost(state);
    if (this->m_dead)
        return;
    clearCollisionLogs(state);
    layer->checkCollisions(state, gdColDt, false);
    hideGhost(state);
}

#define EB_COPY(field) destination->field = source->field

void ShadowSimulator::copyPlayerState(PlayerObject* destination, PlayerObject* source) {
    if (!destination || !source)
        return;

    destination->copyAttributes(source);
    destination->setPosition(source->getPosition());
    destination->setRotation(source->getRotation());
    destination->setScaleX(source->getScaleX());
    destination->setScaleY(source->getScaleY());
    hideGhost(destination);

    EB_COPY(m_position);
    EB_COPY(m_yVelocity);
    EB_COPY(m_fallSpeed);
    EB_COPY(m_gravity);
    EB_COPY(m_gravityMod);
    EB_COPY(m_speedMultiplier);
    EB_COPY(m_yStart);
    EB_COPY(m_playerSpeed);
    EB_COPY(m_playerSpeedAC);
    EB_COPY(m_vehicleSize);
    EB_COPY(m_isUpsideDown);
    EB_COPY(m_isSideways);
    EB_COPY(m_isGoingLeft);
    EB_COPY(m_isShip);
    EB_COPY(m_isBall);
    EB_COPY(m_isBird);
    EB_COPY(m_isSwing);
    EB_COPY(m_isDart);
    EB_COPY(m_isRobot);
    EB_COPY(m_isSpider);
    EB_COPY(m_isSecondPlayer);
    EB_COPY(m_isPlatformer);
    EB_COPY(m_platformerXVelocity);
    EB_COPY(m_platformerVelocityRelated);
    EB_COPY(m_xVelocityRelated);
    EB_COPY(m_xVelocityRelated2);
    EB_COPY(m_yVelocityRelated);
    EB_COPY(m_yVelocityRelated3);
    EB_COPY(m_reverseRelated);
    EB_COPY(m_maybeReverseSpeed);
    EB_COPY(m_maybeReverseAcceleration);
    EB_COPY(m_reverseSync);
    EB_COPY(m_wasTeleported);
    EB_COPY(m_fixGravityBug);
    EB_COPY(m_shipRotation);
    EB_COPY(m_accelerationOrSpeed);
    EB_COPY(m_isAccelerating);
    EB_COPY(m_maybeIsFalling);
    EB_COPY(m_maybeIsBoosted);
    EB_COPY(m_decreaseBoostSlide);
    EB_COPY(m_physDeltaRelated);
    EB_COPY(m_totalTime);
    EB_COPY(m_gameModeChangedTime);
    EB_COPY(m_lastMovedTime);

    EB_COPY(m_isDashing);
    EB_COPY(m_dashX);
    EB_COPY(m_dashY);
    EB_COPY(m_dashAngle);
    EB_COPY(m_dashStartTime);
    EB_COPY(m_dashRing);

    EB_COPY(m_isOnGround);
    EB_COPY(m_isOnGround2);
    EB_COPY(m_isOnGround3);
    EB_COPY(m_isOnGround4);
    EB_COPY(m_lastGroundObject);
    EB_COPY(m_maybeLastGroundObject);
    EB_COPY(m_preLastGroundObject);
    EB_COPY(m_lastGroundedPos);
    EB_COPY(m_groundYVelocity);
    EB_COPY(m_groundObjectMaterial);
    EB_COPY(m_lastLandTime);
    EB_COPY(m_lastJumpTime);
    EB_COPY(m_lastFlipTime);
    EB_COPY(m_lastSpiderFlipTime);
    EB_COPY(m_collidedObject);
    EB_COPY(m_collidingWithLeft);
    EB_COPY(m_collidingWithRight);
    EB_COPY(m_lastCollisionBottom);
    EB_COPY(m_lastCollisionTop);
    EB_COPY(m_lastCollisionLeft);
    EB_COPY(m_lastCollisionRight);
    EB_COPY(m_collidedTopMinY);
    EB_COPY(m_collidedBottomMaxY);
    EB_COPY(m_collidedLeftMaxX);
    EB_COPY(m_collidedRightMinX);
    EB_COPY(m_maybeIsColliding);
    EB_COPY(m_objectSnappedTo);
    EB_COPY(m_snapDistance);
    EB_COPY(m_maybeTouchedBreakableBlock);
    EB_COPY(m_isOnIce);
    EB_COPY(m_isSliding);
    EB_COPY(m_isSlidingRight);
    EB_COPY(m_maybeSlidingTime);
    EB_COPY(m_maybeSlidingStartTime);
    EB_COPY(m_changedDirectionsTime);
    EB_COPY(m_maybeChangedDirectionAngle);
    EB_COPY(m_isMoving);
    EB_COPY(m_maybeHasStopped);
    EB_COPY(m_scaleXRelated);
    EB_COPY(m_scaleXRelated2);
    EB_COPY(m_scaleXRelated3);
    EB_COPY(m_scaleXRelated4);
    EB_COPY(m_scaleXRelated5);
    EB_COPY(m_scaleXRelatedTime);

    EB_COPY(m_isOnSlope);
    EB_COPY(m_wasOnSlope);
    EB_COPY(m_slopeVelocity);
    EB_COPY(m_slopeAngle);
    EB_COPY(m_slopeAngleRadians);
    EB_COPY(m_slopeStartTime);
    EB_COPY(m_slopeEndTime);
    EB_COPY(m_slopeRotation);
    EB_COPY(m_currentSlopeYVelocity);
    EB_COPY(m_yVelocityBeforeSlope);
    EB_COPY(m_currentSlope);
    EB_COPY(m_currentSlope2);
    EB_COPY(m_currentPotentialSlope);
    EB_COPY(m_potentialSlopeMap);
    EB_COPY(m_isCollidingWithSlope);
    EB_COPY(m_collidingWithSlopeId);
    EB_COPY(m_slopeFlipGravityRelated);
    EB_COPY(m_slopeSlidingMaybeRotated);
    EB_COPY(m_isCurrentSlopeTop);
    EB_COPY(m_maybeUpsideDownSlope);
    EB_COPY(m_maybeSlopeForce);
    EB_COPY(m_maybeGoingCorrectSlopeDirection);

    EB_COPY(m_wasJumpBuffered);
    EB_COPY(m_wasRobotJump);
    EB_COPY(m_fixRobotJump);
    EB_COPY(m_stateJumpBuffered);
    EB_COPY(m_stateRingJump);
    EB_COPY(m_stateRingJump2);
    EB_COPY(m_touchedRing);
    EB_COPY(m_touchedCustomRing);
    EB_COPY(m_touchedGravityPortal);
    EB_COPY(m_touchedPad);
    EB_COPY(m_padRingRelated);
    EB_COPY(m_ringJumpRelated);
    EB_COPY(m_ringRelatedSet);
    EB_COPY(m_jumpPadRelated);
    EB_COPY(m_blackOrbRelated);
    EB_COPY(m_hasEverJumped);
    EB_COPY(m_hasEverHitRing);
    EB_COPY(m_lastPortalPos);
    EB_COPY(m_lastActivatedPortal);
    EB_COPY(m_touchedRings);

    EB_COPY(m_stateOnGround);
    EB_COPY(m_stateUnk);
    EB_COPY(m_stateNoStickX);
    EB_COPY(m_stateNoStickY);
    EB_COPY(m_stateUnk2);
    EB_COPY(m_stateBoostX);
    EB_COPY(m_stateBoostY);
    EB_COPY(m_maybeStateForce2);
    EB_COPY(m_stateScale);
    EB_COPY(m_stateNoAutoJump);
    EB_COPY(m_stateDartSlide);
    EB_COPY(m_stateHitHead);
    EB_COPY(m_stateFlipGravity);
    EB_COPY(m_stateForce);
    EB_COPY(m_stateForceVector);
    EB_COPY(m_affectedByForces);

    EB_COPY(m_isLocked);
    EB_COPY(m_controlsDisabled);
    EB_COPY(m_inputsLocked);
    EB_COPY(m_holdingRight);
    EB_COPY(m_holdingLeft);
    EB_COPY(m_leftPressedFirst);

    EB_COPY(m_jumpBuffered);
    EB_COPY(m_holdingButtons);
    destination->m_isDead = false;

    if (destination->m_touchingRings) {
        destination->m_touchingRings->removeAllObjects();
        if (source->m_touchingRings) {
            for (unsigned index = 0; index < source->m_touchingRings->count(); ++index) {
                destination->m_touchingRings->addObject(
                    source->m_touchingRings->objectAtIndex(index));
            }
        }
    }

    clearCollisionLogs(destination);

    if (source->m_isDart) {
        // local helper via instance method — static caller uses get()
        get().copyWaveTrail(destination, source);
    }

    // Ghost is created as icon cube (create 1,1). Mode flags alone leave a
    // cube-sized collide rect → false deaths on tight wave. Rebuild body, then
    // re-apply pose/holds switchedToMode may clobber.
    {
        GameObjectType portal = GameObjectType::CubePortal;
        if (source->m_isDart)
            portal = GameObjectType::WavePortal;
        else if (source->m_isShip)
            portal = GameObjectType::ShipPortal;
        else if (source->m_isBall)
            portal = GameObjectType::BallPortal;
        else if (source->m_isBird)
            portal = GameObjectType::UfoPortal;
        else if (source->m_isRobot)
            portal = GameObjectType::RobotPortal;
        else if (source->m_isSpider)
            portal = GameObjectType::SpiderPortal;
        else if (source->m_isSwing)
            portal = GameObjectType::SwingPortal;

        auto const position = source->getPosition();
        auto const rotation = source->getRotation();
        float const sx = source->getScaleX();
        float const sy = source->getScaleY();
        double const yVel = source->m_yVelocity;
        float const vehicle = source->m_vehicleSize;
        bool const jumpHeld = source->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];

        destination->setContentSize(source->getContentSize());
        destination->switchedToMode(portal);
        destination->updatePlayerScale();
        destination->updatePlayerArt();

        destination->setPosition(position);
        destination->m_position = source->m_position;
        destination->setRotation(rotation);
        destination->setScaleX(sx);
        destination->setScaleY(sy);
        destination->m_vehicleSize = vehicle;
        destination->m_yVelocity = yVel;
        destination->m_isDead = false;

        int const jumpIdx = static_cast<int>(PlayerButton::Jump);
        if (jumpHeld) {
            destination->m_holdingButtons[jumpIdx] = true;
        } else {
            destination->releaseButton(PlayerButton::Jump);
            destination->m_holdingButtons[jumpIdx] = false;
        }
    }

    // switchedToMode and releaseButton rebuild the ghost after the initial
    // state copy. Restore contact/input state afterwards so a grounded ball
    // can actually flip on the first simulated tick.
    EB_COPY(m_isOnGround);
    EB_COPY(m_isOnGround2);
    EB_COPY(m_isOnGround3);
    EB_COPY(m_isOnGround4);
    EB_COPY(m_isOnSlope);
    EB_COPY(m_wasOnSlope);
    EB_COPY(m_currentSlope);
    EB_COPY(m_currentSlope2);
    EB_COPY(m_stateOnGround);
    EB_COPY(m_jumpBuffered);
    EB_COPY(m_wasJumpBuffered);
    EB_COPY(m_stateJumpBuffered);
    EB_COPY(m_stateRingJump);
    EB_COPY(m_stateRingJump2);
    EB_COPY(m_hasEverJumped);
    EB_COPY(m_hasEverHitRing);
    EB_COPY(m_holdingButtons);

    hideGhost(destination);
    clearCollisionLogs(destination);
}

#undef EB_COPY

size_t ShadowSimulator::diagCaptureTransformCount(PlayLayer* layer) {
    if (!layer)
        return 0;
    this->beginCapture(layer);
    size_t const count = this->m_savedTransforms.size();
    this->restoreCapture(layer);
    return count;
}

int ShadowSimulator::captureStartSnapshot(PlayerObject* real) {
    if (!real || !m_snapshots[START_SNAPSHOT_SLOT])
        return -1;
    copyPlayerState(m_snapshots[START_SNAPSHOT_SLOT], real);
    return START_SNAPSHOT_SLOT;
}

int ShadowSimulator::captureSnapshot(PlayerObject* real) {
    if (!real || !m_snapshots[0])
        return -1;
    // Never clobber attempt-start (bake) or parity anchors (14–15).
    if (m_nextSnapshot <= START_SNAPSHOT_SLOT || m_nextSnapshot >= SNAPSHOT_RING_END) {
        m_nextSnapshot = SNAPSHOT_RING_BEGIN;
    }
    int const slot = m_nextSnapshot;
    ++m_nextSnapshot;
    if (m_nextSnapshot >= SNAPSHOT_RING_END) {
        m_nextSnapshot = SNAPSHOT_RING_BEGIN;
    }
    copyPlayerState(m_snapshots[slot], real);
    return slot;
}

int ShadowSimulator::captureParitySnapshot(PlayerObject* real, int which) {
    if (!real || !m_snapshots[0])
        return -1;
    int const slot = (which & 1) ? PARITY_SLOT_B : PARITY_SLOT_A;
    if (!m_snapshots[slot])
        return -1;
    copyPlayerState(m_snapshots[slot], real);
    return slot;
}

PlayerObject* ShadowSimulator::snapshotSlot(int slot) const {
    if (!(slot < 0 || slot >= SNAPSHOT_SLOTS)) {
        return m_snapshots[slot];
    }
    return nullptr;
}

ShadowSimulator::Model ShadowSimulator::modelFor(PlayerObject* player, int limit, float capMul) {
    Model m;
    if (!player)
        return m;
    auto const params = StageConfig::paramsFor(player);
    m.slack = std::max(0, params.slack);

    // Commit 15: planCap from AssistHorizon — long only when SimTrust OK.
    bool const trustOk = SimTrust::get().okAt(player->getPositionX());
    auto const kind = StageConfig::kindOf(player);
    auto const caps = AssistHorizon::resolve(kind, trustOk, limit, capMul, params.cap,
                                             StageConfig::lookAheadCap());
    m.cap = caps.planCap;

    if (player->m_isDart) {
        m.continuous = true;
    } else if (player->m_isShip || player->m_isSwing) {
        m.continuous = true;
    } else if (player->m_isBird) {
        m.continuous = true;
        m.tapRelease = true;
    } else if (player->m_isSpider) {
        m.continuous = true;
        m.tapRelease = true;
    } else if (player->m_isRobot) {
        m.tapRelease = false;
    } else {
        m.tapRelease = true;
    }
    return m;
}

bool ShadowSimulator::isHazardLike(GameObject* object) {
    if (!object)
        return false;
    switch (object->m_objectType) {
    case GameObjectType::Hazard:
    case GameObjectType::AnimatedHazard:
        return true;
    default:
        return false;
    }
}

ThreatZone ShadowSimulator::buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj) {
    ThreatZone z;
    z.valid = true;
    z.startX = deathX - 20.f;
    z.endX = deathX + 20.f;
    if (deathObj) {
        float const x = deathObj->getPositionX();
        float half = deathObj->getContentSize().width * std::fabs(deathObj->getScaleX()) * 0.5f;
        if (half < 12.f)
            half = 12.f;
        z.startX = std::min(z.startX, x - half);
        z.endX = std::max(z.endX, x + half);
    }
    if (!layer || !layer->m_objectLayer)
        return z;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return z;

    class HazardSpan {
    public:
        float lo = 0.f;
        float hi = 0.f;

    private:
    };
    std::vector<HazardSpan> localHazards;
    localHazards.reserve(128);
    constexpr float kMaxClusterRadiusX = 800.f;
    float const winLo = z.startX - kMaxClusterRadiusX;
    float const winHi = z.endX + kMaxClusterRadiusX;
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!isHazardLike(object))
            continue;
        float const x = object->getPositionX();
        if (x < winLo || x > winHi)
            continue;
        float half = object->getContentSize().width * std::fabs(object->getScaleX()) * 0.5f;
        if (half < 10.f)
            half = 10.f;
        localHazards.push_back({x - half, x + half});
    }

    bool grew = true;
    int guard = 0;
    while (grew && guard++ < 32) {
        grew = false;
        for (auto const& h : localHazards) {
            if (h.hi + kClusterGapX < z.startX || h.lo - kClusterGapX > z.endX)
                continue;
            if (h.lo < z.startX) {
                z.startX = h.lo;
                grew = true;
            }
            if (h.hi > z.endX) {
                z.endX = h.hi;
                grew = true;
            }
        }
    }
    return z;
}

bool ShadowSimulator::spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit,
                                     float deltaTime) {
    if (!layer || !real || deltaTime <= 0.f)
        return false;
    int slack = 0;
    if (real) {
        slack = StageConfig::paramsFor(real).slack;
    }
    return TravelForesight::hasOrbOrPadAhead(layer, real, limit, deltaTime, slack);
}

bool ShadowSimulator::spatialHazardProximity(PlayLayer* layer, PlayerObject* real,
                                             float proximityX) {
    if (!layer || !real || !layer->m_objectLayer || proximityX <= 0.f)
        return false;

    float const playerX = real->getPositionX();
    float const playerY = real->getPositionY();
    float const yReach = real->m_isShip || real->m_isDart || real->m_isSwing ? 100.f : 70.f;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return false;

    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!isHazardLike(object))
            continue;

        float const objectX = object->getPositionX();
        float const objectY = object->getPositionY();
        float half = object->getContentSize().width * std::fabs(object->getScaleX()) * 0.5f;
        if (half < 8.f)
            half = 8.f;
        float const oLo = objectX - half;
        float const oHi = objectX + half;
        // Ahead of player, overlapping [px, px+proximityX].
        if (oHi < playerX - 4.f)
            continue;
        if (oLo > playerX + proximityX)
            continue;
        if (std::fabs(objectY - playerY) > yReach)
            continue;
        return true;
    }
    return false;
}

bool ShadowSimulator::shouldWaveHazardProximityPass(bool isWave, bool nearHazard, bool noOrbTarget,
                                                    bool foresightOrbPad, bool pressDown) {
    // Release assist = delay the UP (keep holding). Never early-Pass that near hazards.
    if (!pressDown)
        return false;
    if (!isWave)
        return false;
    if (!nearHazard)
        return false;
    if (!noOrbTarget)
        return false;
    // Miss-assist: orb/pad in travel cone → keep searching, do not Pass.
    if (!(foresightOrbPad)) {
        return true;
    }
    return false;
}

bool ShadowSimulator::shouldWaveFutileDelayPass(bool isWave, bool orbIntent, int bestSurviveBin,
                                                float bestEndXGain, bool foresightOrbPad,
                                                bool pressDown) {
    if (!pressDown)
        return false;
    if (!isWave)
        return false;
    if (orbIntent)
        return false;
    if (foresightOrbPad)
        return false;
    if (bestSurviveBin > 0)
        return false;
    if (!(bestEndXGain >= kFutileEndXGain)) {
        return true;
    }
    return false;
}

bool ShadowSimulator::deathLooksLikeAscent(float playerY, float deathY, bool upsideDown,
                                           float marginY) {
    if (marginY < 0.f)
        marginY = 0.f;
    if (upsideDown) {
        // Flipped: "ceiling" is below in world Y.
        return deathY < playerY - marginY;
    }
    return deathY > playerY + marginY;
}

bool ShadowSimulator::classifyIdleSafer(bool continuous, bool pressDown, bool baselineDied,
                                        int baselineDeathTick, float baselineDeathX, bool d0Died,
                                        int d0DeathTick, float d0DeathX) {
    if (!continuous || !pressDown)
        return false;
    // Idle must look better than pressing now.
    if (!d0Died)
        return false;
    if (!baselineDied)
        return true;
    if (baselineDeathTick < 0 || d0DeathTick < 0)
        return !baselineDied;
    if (baselineDeathTick > d0DeathTick)
        return true;
    if (baselineDeathTick == d0DeathTick && baselineDeathX > d0DeathX + 8.f) {
        return true;
    }
    return false;
}

bool ShadowSimulator::classifyAscentUseful(bool continuous, bool pressDown, bool d0Viable,
                                           int d0SurviveBin, float d0EndX, bool laterViable,
                                           int laterSurviveBin, float laterEndX, bool laterAlive) {
    if (!continuous || !pressDown)
        return false;
    // Dead / soft Landing-LateCrash ghosts must not invent ascentUseful.
    if (!laterViable || !laterAlive)
        return false;
    if (!d0Viable)
        return true;
    if (laterSurviveBin > d0SurviveBin)
        return true;
    if (!(laterEndX > d0EndX + kFutileEndXGain)) {
        return false;
    }
    return true;
}

int ShadowSimulator::pickPreferredRelease(int earliest, int latest, bool continuous) {
    if (earliest < 0)
        return -1;
    if (latest < earliest)
        latest = earliest;
    if (!(continuous)) {
        return earliest + (latest - earliest) / 2;
    }
    return earliest;
}

int ShadowSimulator::autoRobustKHints(bool waveLike, bool isWave, float playerSpeed,
                                      float dtVariance, bool spamDense) {
    int offset = kWindowMarginK; // 1
    if (waveLike)
        offset = 2;
    // Wave spam corridors: keep at least 2; at speed bump to 3 so ±1 neighbors
    // of the pick stay clear (Tight ticks-wave=2 may still fall back — see docs).
    if (isWave && spamDense) {
        offset = std::max(offset, 2);
        if (playerSpeed > 1.05f)
            offset = std::max(offset, 3);
    }
    if (playerSpeed > 1.1f)
        offset = std::max(offset, 2);
    if (playerSpeed > 1.5f)
        offset = 3;
    if (dtVariance > 0.08f)
        offset = std::min(3, offset + 1);
    return std::clamp(offset, 1, 3);
}

int ShadowSimulator::autoRobustK(PlayerObject* real, float dtVariance, bool spamDense) {
    bool waveLike = false;
    bool isWave = false;
    float speed = 1.f;
    if (real) {
        isWave = real->m_isDart;
        waveLike = real->m_isDart || real->m_isShip || real->m_isSwing;
        speed = real->m_playerSpeed;
    }
    return autoRobustKHints(waveLike, isWave, speed, dtVariance, spamDense);
}

ShadowSimulator::RunResult ShadowSimulator::runOne(PlayLayer* layer, PlayerObject* real, bool down,
                                                   int delay, int limit, float deltaTime,
                                                   float firstDt, Model const& model,
                                                   int releaseAfter, ThreatZone const* zone,
                                                   int* tickBudgetLeft) {
    RunResult r;
    (void)limit;
    copyPlayerState(m_shadow, real);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_toggleUnknownHit = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* shadowPlayer = m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    int groundedStreak = 0;
    int settledAfterClear = 0;
    bool pastZone = false;
    bool const initialGravity = shadowPlayer->m_isUpsideDown;
    bool ballFlipped = false;
    bool ballAirborne = false;
    int ballLandingTicks = 0;
    int const maxTicks = std::max(std::max(0, limit), delay) + std::max(1, model.cap);

    auto finish = [&](RunResult output) {
        output.activatedRing = m_activatedRing;
        output.ringCount = m_ringCount;
        output.endX = shadowPlayer->getPositionX();
        output.wasDashing = shadowPlayer->m_isDashing;
        output.toggleUnknown = m_toggleUnknownHit;
        if (output.toggleUnknown)
            output.unknownTrunc = true;
        output.ticksRan =
            std::max(output.ticksRan, output.deathTick >= 0 ? output.deathTick + 1 : 0);
        if (output.ticksBeforeNeutral <= 0)
            output.ticksBeforeNeutral = output.ticksRan;
        return output;
    };

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft)
            return false;
        if (*tickBudgetLeft <= 0)
            return true;
        --(*tickBudgetLeft);
        return false;
    };

    if (delay > 0) {
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase;
        stepPreCollide(layer, shadowPlayer, deltaTime);
        if (m_dead) {
            r.ticksRan = 1;
            r.died = true;
            r.deathTick = 0;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = m_deathObject;
            r.diedBeforeAction = true;
            return finish(r);
        }
        if (m_unknown) {
            r.ticksRan = 1;
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = 0;
            return finish(r);
        }
    }

    for (int tick = 0; tick < maxTicks; ++tick) {
        if (spend())
            break;
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        r.ticksRan = tick + 1;
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase + tick;

        if (!acted && tick == delay) {
            acted = true;
            if (down) {
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && down) {
            if (releaseAfter >= 0 && !tapReleased && tick == delay + releaseAfter) {
                tapReleased = true;
                shadowPlayer->releaseButton(PlayerButton::Jump);
            } else if (releaseAfter < 0 && model.tapRelease && !tapReleased && m_jumped) {
                bool const touchingOrb =
                    m_orbOverlapTick ||
                    (shadowPlayer->m_touchingRings && shadowPlayer->m_touchingRings->count() > 0);
                bool const dashing = shadowPlayer->m_isDashing;
                bool const justHitOrb = m_activatedRing && tick <= delay + 4;
                if (!touchingOrb && !dashing && !justHitOrb) {
                    tapReleased = true;
                    shadowPlayer->releaseButton(PlayerButton::Jump);
                }
            }
        }

        stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        if (m_dead) {
            r.died = true;
            r.deathTick = tick;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = m_deathObject;
            r.diedBeforeAction = !acted;
            return finish(r);
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = tick;
            return finish(r);
        }

        if (model.stopAtBallLanding && acted && shadowPlayer->m_isBall) {
            ballFlipped = ballFlipped || shadowPlayer->m_isUpsideDown != initialGravity;
            bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
            if (ballFlipped && !grounded)
                ballAirborne = true;
            ballLandingTicks = ballAirborne && grounded ? ballLandingTicks + 1 : 0;
            if (ballLandingTicks >= 2) {
                r.landedAfterAction = true;
                return finish(r);
            }
        }

        if (m_jumped && !r.jumped) {
            r.jumped = true;
            r.jumpTick = tick;
        }
        if (shadowPlayer->m_isDashing)
            r.wasDashing = true;

        // G2: after threat zone + settled → neutral (stop extending survive score)
        if (zone && zone->valid && !pastZone) {
            if (shadowPlayer->getPositionX() > zone->endX) {
                bool settled = false;
                if (model.continuous) {
                    settledAfterClear++;
                    settled = settledAfterClear >= 8;
                } else {
                    bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
                    if (grounded)
                        ++groundedStreak;
                    else
                        groundedStreak = 0;
                    settled = groundedStreak >= 2;
                }
                if (settled) {
                    pastZone = true;
                    r.zoneCleared = true;
                    r.ticksBeforeNeutral = tick + 1;
                    // Keep simulating briefly for orb/path info but mark neutral
                }
            } else {
                settledAfterClear = 0;
            }
        }

        if (pastZone && !model.continuePastClear && tick + 1 - r.ticksBeforeNeutral > 24) {
            return finish(r);
        }
    }
    return finish(r);
}

ShadowSimulator::RunResult
ShadowSimulator::runTrajectoryOne(PlayLayer* layer, PlayerObject* real, bool down, int delay,
                                  int limit, float deltaTime, float firstDt, Model const& model,
                                  int releaseAfter, ThreatZone const* zone, int* tickBudgetLeft) {
    // Lighter Live V2 path: same button timing as runOne, no delay-0 orb pre-scan.
    RunResult r;
    (void)limit;
    if (!layer || !real || !m_shadow)
        return r;
    copyPlayerState(m_shadow, real);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_toggleUnknownHit = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* shadowPlayer = m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    int groundedStreak = 0;
    int settledAfterClear = 0;
    bool pastZone = false;
    int const maxTicks = std::max(std::max(0, limit), delay) + std::max(1, model.cap);

    auto finish = [&](RunResult output) {
        output.activatedRing = m_activatedRing;
        output.ringCount = m_ringCount;
        output.endX = shadowPlayer->getPositionX();
        output.wasDashing = shadowPlayer->m_isDashing;
        output.toggleUnknown = m_toggleUnknownHit;
        if (output.toggleUnknown)
            output.unknownTrunc = true;
        output.ticksRan =
            std::max(output.ticksRan, output.deathTick >= 0 ? output.deathTick + 1 : 0);
        if (output.ticksBeforeNeutral <= 0)
            output.ticksBeforeNeutral = output.ticksRan;
        return output;
    };

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft)
            return false;
        if (*tickBudgetLeft <= 0)
            return true;
        --(*tickBudgetLeft);
        return false;
    };

    for (int tick = 0; tick < maxTicks; ++tick) {
        if (spend())
            break;
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        r.ticksRan = tick + 1;
        this->m_simAbsTick = this->m_simTickBase + tick;

        if (!acted && tick == delay) {
            acted = true;
            if (down) {
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && down && releaseAfter >= 0 && !tapReleased &&
                   tick == delay + releaseAfter) {
            tapReleased = true;
            shadowPlayer->releaseButton(PlayerButton::Jump);
        }

        stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        if (m_dead) {
            r.died = true;
            r.deathTick = tick;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = m_deathObject;
            r.diedBeforeAction = !acted;
            return finish(r);
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = tick;
            return finish(r);
        }

        if (m_jumped && !r.jumped) {
            r.jumped = true;
            r.jumpTick = tick;
        }
        if (shadowPlayer->m_isDashing)
            r.wasDashing = true;

        if (zone && zone->valid && !pastZone) {
            if (shadowPlayer->getPositionX() > zone->endX) {
                bool settled = false;
                if (model.continuous) {
                    settledAfterClear++;
                    settled = settledAfterClear >= 8;
                } else {
                    bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
                    if (grounded)
                        ++groundedStreak;
                    else
                        groundedStreak = 0;
                    settled = groundedStreak >= 2;
                }
                if (settled) {
                    pastZone = true;
                    r.zoneCleared = true;
                    r.ticksBeforeNeutral = tick + 1;
                }
            } else {
                settledAfterClear = 0;
            }
        }

        if (pastZone && !model.continuePastClear && tick + 1 - r.ticksBeforeNeutral > 24) {
            return finish(r);
        }
    }
    return finish(r);
}

ShadowSimulator::RunResult ShadowSimulator::runBaseline(PlayLayer* layer, PlayerObject* real,
                                                        bool down, float deltaTime, float firstDt,
                                                        Model const& model, int* tickBudgetLeft) {
    RunResult r;
    copyPlayerState(m_shadow, real);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* shadowPlayer = m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    int const maxTicks = std::max(1, model.cap);
    for (int tick = 0; tick < maxTicks; ++tick) {
        if (tickBudgetLeft) {
            if (*tickBudgetLeft <= 0)
                break;
            --(*tickBudgetLeft);
        }
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        this->m_simAbsTick = this->m_simTickBase + tick;
        stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        r.ticksRan = tick + 1;
        r.endX = shadowPlayer->getPositionX();
        if (m_dead) {
            r.died = true;
            r.deathTick = tick;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = m_deathObject;
            return r;
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = tick;
            return r;
        }
    }
    return r;
}

ShadowSimulator::OrbWindow ShadowSimulator::scanOrbWindow(PlayLayer* layer, PlayerObject* real,
                                                          bool down, int scanTicks, float deltaTime,
                                                          float firstDt, int* tickBudgetLeft,
                                                          bool holdJump) {
    OrbWindow win;
    if (!layer || !real || !this->m_shadow || scanTicks <= 0)
        return win;

    this->copyPlayerState(this->m_shadow, real);
    this->m_dead = false;
    this->m_jumped = false;
    this->m_unknown = false;
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_orbOverlapTick = false;
    this->m_deathObject = nullptr;

    auto* shadowPlayer = this->m_shadow;
    // Press-to-reach: hold Jump. Otherwise legacy coast with Jump = !down.
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = holdJump ? true : !down;

    m_orbScanning = true;
    for (int tick = 0; tick < scanTicks; ++tick) {
        if (tickBudgetLeft) {
            if (*tickBudgetLeft <= 0)
                break;
            --(*tickBudgetLeft);
        }
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase + tick;

        stepPreCollide(layer, shadowPlayer, deltaTime);
        bool const touchPre = m_orbOverlapTick || (shadowPlayer->m_touchingRings &&
                                                   shadowPlayer->m_touchingRings->count() > 0);
        if (touchPre) {
            if (!win.found) {
                win.found = true;
                win.first = tick;
            }
            win.last = tick;
        }
        if (m_dead || m_unknown)
            break;

        m_orbOverlapTick = false;
        stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        bool const touchPost = m_orbOverlapTick || (shadowPlayer->m_touchingRings &&
                                                    shadowPlayer->m_touchingRings->count() > 0);
        if (touchPost) {
            if (!win.found) {
                win.found = true;
                win.first = tick;
            }
            win.last = tick;
        }
        if (m_dead || m_unknown)
            break;
    }
    m_orbScanning = false;
    return win;
}

int ShadowSimulatorImplementation::surviveBinOf(int ticks) {
    if (ticks < 24)
        return 0;
    if (ticks < 48)
        return 1;
    if (!(ticks < 96)) {
        return 3;
    }
    return 2;
}

// Ranking / ascent inputs: ticks until death, else full run — not neutral stamp.
int ShadowSimulatorImplementation::rankingSurviveTicks(ShadowSimulator::RunResult const& r) {
    if (r.died && r.deathTick >= 0)
        return r.deathTick + 1;
    if (!(r.ticksRan > 0)) {
        return r.ticksBeforeNeutral;
    }
    return r.ticksRan;
}

bool ShadowSimulatorImplementation::clearsThreatOf(ShadowSimulator::RunResult const& r,
                                                   ThreatZone const& zone) {
    if (!zone.valid) {
        if (r.unknownTrunc)
            return true;
        return !r.died;
    }
    if (!(!r.died)) {
        return r.deathX > zone.endX;
    }
    return r.endX > zone.endX || r.zoneCleared;
}

// Score honesty + ModeSurvival:
// Delay-viable needs clearsThreat (or no zone / zoneCleared). surviveBin alone
// is NOT enough. Unknown never viable.
// Soft (requireAliveForDelay=false): died may still be not-false-lethal when
// Landing / Recoverable / LateCrashAfterClear (bake verify).
// Live arm (requireAliveForDelay=true): Delay-worthy = Alive only; Landing /
// LateCrash never invent a Delay opportunity.
bool ShadowSimulatorImplementation::delayViableHonest(bool diedBeforeAction, bool died,
                                                      bool unknownTrunc, bool toggleUnknown,
                                                      bool clearsThreat, bool zoneCleared,
                                                      bool zoneValid, ModeSurvival::Class survival,
                                                      bool requireAliveForDelay) {
    if (diedBeforeAction || unknownTrunc || toggleUnknown)
        return false;
    if (requireAliveForDelay && ModeSurvival::shouldSkipFalseOpportunity(survival)) {
        return false;
    }
    bool const classOk = requireAliveForDelay ? ModeSurvival::countsAsDelayWorthy(survival)
                                              : ModeSurvival::countsAsSafeForDelay(survival);
    if (died && !classOk)
        return false;
    // Alive path: still need threat clear when a zone exists.
    // Soft LateCrash/Landing/Recoverable already imply clear geometry in classify.
    if (!(!died)) {
        return classOk && (!zoneValid || clearsThreat || zoneCleared);
    }
    return classOk && (!zoneValid || clearsThreat || zoneCleared);
}

ModeSurvival::Input ShadowSimulatorImplementation::survivalInputFrom(
    ShadowSimulator::RunResult const& r, ThreatZone const& zone,
    ShadowSimulator::Model const& model, PlayerObject* real) {
    ModeSurvival::Input in;
    in.continuous = model.continuous;
    if (real) {
        in.isWave = real->m_isDart;
        in.isShipLike = real->m_isShip || real->m_isBird || real->m_isSpider || real->m_isSwing;
        in.isCubeLike = real->m_isRobot || real->m_isBall ||
                        (!real->m_isDart && !real->m_isShip && !real->m_isBird &&
                         !real->m_isSpider && !real->m_isSwing && !model.continuous);
        in.upsideDown = real->m_isUpsideDown;
        in.playerY = real->getPositionY();
    } else {
        in.isCubeLike = !model.continuous;
        in.isWave = model.continuous;
    }
    in.died = r.died;
    in.diedBeforeAction = r.diedBeforeAction;
    in.zoneValid = zone.valid;
    in.zoneCleared = r.zoneCleared;
    in.groundedSettled = r.zoneCleared && r.ticksBeforeNeutral > 0 && !model.continuous;
    in.activatedRing = r.activatedRing;
    in.deathX = r.deathX;
    in.zoneEndX = zone.endX;
    in.deathY = 0.f;
    in.deathTick = r.deathTick;
    in.ticksBeforeNeutral = r.ticksBeforeNeutral;
    return in;
}

bool ShadowSimulatorImplementation::runResultDelaySafe(ShadowSimulator::RunResult const& r,
                                                       ThreatZone const& zone,
                                                       ShadowSimulator::Model const& model,
                                                       PlayerObject* real,
                                                       bool requireAliveForDelay) {
    auto const cls = ModeSurvival::classify(
        ShadowSimulatorImplementation::survivalInputFrom(r, zone, model, real));
    return ShadowSimulatorImplementation::delayViableHonest(
        r.diedBeforeAction, r.died, r.unknownTrunc, r.toggleUnknown,
        ShadowSimulatorImplementation::clearsThreatOf(r, zone), r.zoneCleared, zone.valid, cls,
        requireAliveForDelay);
}

// planSequence exit: unknown truncation is NOT survival.
bool ShadowSimulatorImplementation::exitSurvivedHonest(bool died, bool unknown) {
    return !died && !unknown;
}

// 0 = auto from mode / speed / dt variance / spam density; else clamp 1–3.
int ShadowSimulatorImplementation::resolveRobustK(PlayerObject* real, PlanOptions const& options) {
    int forced = options.robustK;
    if (forced <= 0)
        forced = StageConfig::robustK();
    if (!(forced > 0)) {
        return ShadowSimulator::autoRobustK(real, options.dtVariance, options.spamDense);
    }
    return std::clamp(forced, 1, 3);
}

ShadowSimulator::VerifyDelayResult
ShadowSimulator::verifyDelay(PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
                             float deltaTime, float firstDt, PlanOptions const& options) {
    VerifyDelayResult out;
    if (!layer || !real || !m_shadow || limit < 0 || deltaTime <= 0.f)
        return out;
    if (firstDt <= 0.f || firstDt > deltaTime)
        firstDt = deltaTime;
    delay = std::max(0, delay);

    Model model = modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    int tickBudget = options.tickBudget > 0 ? options.tickBudget : kDefaultTickBudget;
    int const tickBudgetStart = tickBudget;
    auto const wall0 = std::chrono::steady_clock::now();
    auto wallExceeded = [&]() -> bool {
        if (options.budgetUs <= 0)
            return false;
        auto elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - wall0)
                                       .count();
        return elapsedMicroseconds >= options.budgetUs;
    };
    auto fillSpent = [&]() {
        out.ticksSpent = std::max(0, tickBudgetStart - tickBudget);
        out.usSpent = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - wall0)
                                           .count());
    };

    auto linkedDual = [&]() -> bool {
        if (!layer || !layer->m_player2 || !real)
            return false;
        if (!layer->m_gameState.m_isDualMode)
            return false;
        if (auto* gm = GameManager::sharedState()) {
            if (gm->getGameVariable("0010"))
                return false;
        }
        return real == layer->m_player1 || real == layer->m_player2;
    };

    this->m_simTickBase = std::max(0, options.attemptStartTick);
    this->m_simAbsTick = -1;
    SimSessionGuard session(*this, layer);

    ThreatZone zone{};
    // Probe d0 to build the same threat zone Live uses. When delay==0 the
    // planned candidate is identical — reuse that runOne (no second sim).
    auto c0 = this->runOne(layer, real, down, 0, limit, deltaTime, firstDt, model, -1, nullptr,
                           &tickBudget);
    this->resetCaptureForNextRun(layer);
    if (c0.died) {
        zone = buildThreatZone(layer, c0.deathX, c0.deathObject);
        out.threatZone = zone;
    }
    if (tickBudget <= 0 || wallExceeded()) {
        out.budgetHit = true;
        fillSpent();
        return out;
    }

    RunResult r;
    if (delay == 0) {
        r = c0;
    } else {
        r = this->runOne(layer, real, down, delay, limit, deltaTime, firstDt, model, -1,
                         zone.valid ? &zone : nullptr, &tickBudget);
        this->resetCaptureForNextRun(layer);
    }

    out.diedBeforeAction = r.diedBeforeAction;
    out.clearsThreat = ShadowSimulatorImplementation::clearsThreatOf(r, zone);
    out.unknown = r.unknown || r.unknownTrunc || r.toggleUnknown;
    // clearsThreat honesty + ModeSurvival (soft or Alive-only via options).
    bool const requireAlive = options.requireAliveForDelay;
    bool viable =
        ShadowSimulatorImplementation::runResultDelaySafe(r, zone, model, real, requireAlive);

    // Linked dual: still one runOne for the other player (even when delay==0).
    if (viable && linkedDual()) {
        PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
        if (other && tickBudget > 0 && !wallExceeded()) {
            auto r2 = this->runOne(layer, other, down, delay, limit, deltaTime, firstDt, model, -1,
                                   zone.valid ? &zone : nullptr, &tickBudget);
            this->resetCaptureForNextRun(layer);
            if (!ShadowSimulatorImplementation::runResultDelaySafe(r2, zone, model, other,
                                                                   requireAlive)) {
                viable = false;
                out.clearsThreat = false;
            }
        }
    }

    if (tickBudget <= 0 || wallExceeded())
        out.budgetHit = true;

    fillSpent();
    // Budget exhaustion → do not trust bake (caller falls through / Pass).
    out.ok = viable && !out.budgetHit;
    return out;
}

SimPlan ShadowSimulator::plan(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                              float deltaTime, float firstDt, PlanOptions const& options) {
    PerfTrace::Scope const scope("plan");
    static int s_planId = 1;
    SimPlan plan;
    plan.smartDepthUsed = 1;
    plan.planId = s_planId++;
    if (!layer || !real || !this->m_shadow || limit < 0 || deltaTime <= 0.f) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassInvalidArgs;
        if (limit >= 0) {
            DevRejectLedger::BuildInput emptyIn;
            emptyIn.limit = limit;
            emptyIn.doPass = true;
            emptyIn.chosenDelay = 0;
            emptyIn.includeScores = DevConfig::recordCands();
            auto ledger = DevRejectLedger::build(emptyIn);
            plan.candNotes = std::move(ledger.notes);
            plan.candNotesTruncated = ledger.truncated;
        }
        return plan;
    }
    if (firstDt <= 0.f || firstDt > deltaTime)
        firstDt = deltaTime;

    Model model = modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    plan.continuous = model.continuous;
    int const robustK = ShadowSimulatorImplementation::resolveRobustK(real, options);
    float const latencyWeight = std::max(0.f, options.latencyWeight);

    int tickBudget = options.tickBudget > 0 ? options.tickBudget : kDefaultTickBudget;
    // A/B compare runs both arms' semantics over the same cand set; allow 2× ticks.
    if (StageConfig::assistVerifyAb())
        tickBudget *= 2;
    auto const wall0 = std::chrono::steady_clock::now();
    auto wallExceeded = [&]() -> bool {
        if (options.budgetUs <= 0)
            return false;
        auto elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - wall0)
                                       .count();
        return elapsedMicroseconds >= options.budgetUs;
    };

    auto linkedDual = [&]() -> bool {
        if (!layer || !layer->m_player2 || !real)
            return false;
        if (!layer->m_gameState.m_isDualMode)
            return false;
        // "0010" Flip2PlayerControls → split buffers; linked = shared jump for both.
        if (auto* gm = GameManager::sharedState()) {
            if (gm->getGameVariable("0010"))
                return false;
        }
        return real == layer->m_player1 || real == layer->m_player2;
    };

    // Live V2 only: Trajectory cand eval. V1 / Hybrid verify stay on ShadowPlayer.
    bool const useTrajEngine =
        !options.legacyPick && options.assistEngine == StageConfig::AssistEngineKind::Trajectory;
    plan.trajectoryEngine = useTrajEngine;
    auto runEngine = [&](PlayerObject* who, int delay, int releaseAfter,
                         ThreatZone const* z) -> RunResult {
        if (useTrajEngine) {
            return runTrajectoryOne(layer, who, down, delay, limit, deltaTime, firstDt, model,
                                    releaseAfter, z, &tickBudget);
        }
        return runOne(layer, who, down, delay, limit, deltaTime, firstDt, model, releaseAfter, z,
                      &tickBudget);
    };

    this->m_simTickBase = std::max(0, options.attemptStartTick);
    this->m_simAbsTick = -1;
    SimSessionGuard session(*this, layer);
    bool sawToggleUnknown = false;
    bool const hadToggleTargets = !this->m_toggleTargetIds.empty();

    bool const continuousControl = real->m_isDart || real->m_isShip || real->m_isSwing;
    bool const inputWindow = options.requireAliveForDelay && (down || continuousControl);
    model.stopAtBallLanding = inputWindow && real->m_isBall && !useTrajEngine && !linkedDual();
    // Continuous steering is local to the input window: a single held direction
    // cannot cover the later turns of a corridor. Discrete modes retain the
    // existing 96-tick maximum. Explicit horizon settings take precedence.
    if (inputWindow && StageConfig::paramsFor(real).cap == 0 && StageConfig::lookAheadCap() == 0) {
        if (continuousControl) {
            model.cap = MissAssistContracts::continuousLookAhead(limit);
        } else {
            model.cap = std::min(model.cap, std::max(1, 96 - limit));
        }
    }
    if (!inputWindow) {
        auto base = runBaseline(layer, real, down, deltaTime, firstDt, model, &tickBudget);
        this->resetCaptureForNextRun(layer);
        if (base.died) {
            plan.baselineDeathTick = base.deathTick;
            plan.baselineDeathObject = base.deathObject;
            plan.baselineDeathX = base.deathX;
        }
    }

    bool const orbAssist = StageConfig::assistOrbs() && down;
    OrbWindow orbHint{};
    bool spatialHint = false;
    if (orbAssist && !inputWindow) {
        int const scanTicks = OpportunityProbe::orbScanTicks(limit, model.slack);
        orbHint = this->scanOrbWindow(layer, real, down, scanTicks, deltaTime, firstDt, &tickBudget,
                                      false);
        this->resetCaptureForNextRun(layer);
        // Press-to-reach: second scan with Jump held if coast missed (budget-capped).
        if (!orbHint.found && tickBudget >= OpportunityProbe::kMinBudgetForProbe) {
            int const reachTicks = OpportunityProbe::pressReachScanTicks(limit, model.slack);
            auto held = this->scanOrbWindow(layer, real, down, reachTicks, deltaTime, firstDt,
                                            &tickBudget, true);
            this->resetCaptureForNextRun(layer);
            if (held.found) {
                orbHint = held;
            }
        }
        if (orbHint.found)
            plan.usedOrbWindow = true;
        spatialHint = this->spatialOrbHint(layer, real, limit, deltaTime);
        plan.foresightOrbPad = spatialHint;
    }

    ThreatZone zone{};
    std::vector<SimulationCandidate> cands(static_cast<size_t>(limit) + 1);
    std::vector<char> candReady(static_cast<size_t>(limit) + 1, 0);

    int ledgerActMin = -1;
    int ledgerActMax = -1;
    bool ledgerOrbIntent = false;
    bool ledgerPreferEarlyExit = false;

    auto publishLedger = [&](int chosenDelay, bool doPassFlag, DevRejectLedger::EarlyExit early) {
        DevRejectLedger::BuildInput in;
        in.limit = limit;
        in.chosenDelay = chosenDelay;
        in.doPass = doPassFlag;
        in.latencyWeight = latencyWeight;
        in.orbIntent = ledgerOrbIntent;
        in.actMin = ledgerActMin;
        in.actMax = ledgerActMax;
        in.legacyPick = options.legacyPick;
        in.earlyExit = early;
        in.includeScores = DevConfig::recordCands();
        in.snaps.assign(static_cast<size_t>(limit) + 1, {});
        for (int delay = 0; delay <= limit; ++delay) {
            if (!candReady[static_cast<size_t>(delay)])
                continue;
            auto const& candidate = cands[static_cast<size_t>(delay)];
            auto& s = in.snaps[static_cast<size_t>(delay)];
            s.evaluated = true;
            s.viable = candidate.viable;
            s.diedBeforeAction = candidate.diedBeforeAction;
            s.dualFailed = candidate.dualFailed;
            s.unknownTrunc = candidate.unknownTrunc;
            s.clearsThreat = candidate.clearsThreat;
            s.activatedRing = candidate.activatedRing;
            s.robustKFailed = candidate.robustKFailed;
            s.verifyFailed = candidate.verifyFailed;
            s.surviveBin = candidate.surviveBin;
            s.endX = candidate.endX;
        }
        auto ledger = DevRejectLedger::build(in);
        for (auto& note : ledger.notes) {
            if (note.delay < 0 || note.delay > limit || !candReady[note.delay])
                continue;
            auto const& run = cands[note.delay].raw;
            note.detail += fmt::format(" ticks={} died={} deathTick={} unknown={} ballLanding={}",
                                       run.ticksRan, run.died, run.deathTick,
                                       run.unknown || run.unknownTrunc, run.landedAfterAction);
        }
        plan.candNotes = std::move(ledger.notes);
        plan.candNotesTruncated = ledger.truncated;
    };

    auto evalDelay = [&](int delayValue, int releaseAfter) -> SimulationCandidate {
        SimulationCandidate candidate;
        candidate.delay = delayValue;
        if (tickBudget <= 0 || wallExceeded()) {
            plan.budgetHit = true;
            return candidate;
        }
        auto result = runEngine(real, delayValue, releaseAfter, zone.valid ? &zone : nullptr);
        this->resetCaptureForNextRun(layer);
        ++plan.candidatesTried;
        candidate.raw = result;
        candidate.diedBeforeAction = result.diedBeforeAction;
        candidate.activatedRing = result.activatedRing;
        candidate.unknownTrunc = result.unknownTrunc;
        candidate.toggleUnknown = result.toggleUnknown;
        if (candidate.toggleUnknown || (candidate.unknownTrunc && hadToggleTargets)) {
            sawToggleUnknown = true;
        }
        candidate.endX = result.endX;
        candidate.deathX = result.deathX;
        candidate.jumpTick = result.jumped ? result.jumpTick : delayValue;
        candidate.surviveTicks = ShadowSimulatorImplementation::rankingSurviveTicks(result);
        candidate.surviveBin = ShadowSimulatorImplementation::surviveBinOf(candidate.surviveTicks);
        candidate.requireHold = false; // set only if tap fails and hold-branch wins
        return candidate;
    };

    if (inputWindow) {
        plan.fixedFireTick = true;
        auto const complete = [&](RunResult const& result) {
            return MissAssistContracts::survivesObservation(
                result.died, result.unknown || result.unknownTrunc || result.toggleUnknown,
                result.ticksRan, limit + model.cap, result.landedAfterAction);
        };
        bool const dual = linkedDual();
        plan.reason = AssistReason::PassHopeless;
        for (int delayValue = 0; delayValue <= limit; ++delayValue) {
            auto candidate = evalDelay(delayValue, -1);
            if (candidate.raw.ticksRan == 0) {
                plan.budgetHit = true;
                break;
            }
            bool dead = candidate.raw.died;
            bool safe = complete(candidate.raw);
            if (dual && !dead) {
                auto* other = real == layer->m_player1 ? layer->m_player2 : layer->m_player1;
                if (wallExceeded() || tickBudget <= 0) {
                    plan.budgetHit = true;
                    break;
                }
                auto partner = runEngine(other, delayValue, -1, nullptr);
                resetCaptureForNextRun(layer);
                dead = partner.died;
                safe = safe && complete(partner);
            }
            candidate.viable = safe;
            candidate.clearsThreat = safe;
            cands[delayValue] = candidate;
            candReady[delayValue] = 1;
            if (delayValue == 0) {
                plan.firstDeathTick = candidate.raw.deathTick;
                plan.firstDeathObject = candidate.raw.deathObject;
                if (!dead && !safe) {
                    plan.reason = AssistReason::PassHardUnknown;
                    break;
                }
            }
            if (!safe)
                continue;
            plan.delay = plan.jumpTick = delayValue;
            plan.nowIsSafe = delayValue == 0;
            plan.threatCleared = true;
            plan.reason = delayValue ? AssistReason::DelayThreat : AssistReason::PassD0Safe;
            if (delayValue) {
                plan.action = PlanAction::Delay;
                plan.viableDelays.push_back(delayValue);
            }
            break;
        }
        if (plan.budgetHit)
            plan.reason = AssistReason::PassBudget;
        publishLedger(plan.delay, plan.action == PlanAction::Pass,
                      plan.budgetHit ? DevRejectLedger::EarlyExit::Budget
                                     : DevRejectLedger::EarlyExit::None);
        return plan;
    }

    bool const trustLong = real && SimTrust::get().okAt(real->getPositionX());
    auto const horizonCaps =
        AssistHorizon::resolve(real ? StageConfig::kindOf(real) : GameModeKind::Cube, trustLong,
                               limit, options.capMul, 0, StageConfig::lookAheadCap());
    int const minSurviveLong =
        AssistHorizon::minSurviveTicksForSuccess(horizonCaps.scoreHorizon, trustLong);

    bool const requireAlive = options.requireAliveForDelay;
    auto finalizeViable = [&](SimulationCandidate& candidate, int releaseAfter) {
        candidate.clearsThreat = ShadowSimulatorImplementation::clearsThreatOf(candidate.raw, zone);
        // clearsThreat + ModeSurvival (soft bake vs Live Delay-worthy Alive).
        candidate.viable = ShadowSimulatorImplementation::runResultDelaySafe(
            candidate.raw, zone, model, real, requireAlive);
        // Trust-long — alive path must run far enough to see continue
        // (platform after jump). Budget-truncated short runs are not failed here.
        if (candidate.viable && minSurviveLong > 0) {
            bool const truncated = !candidate.raw.died && !candidate.raw.unknownTrunc &&
                                   candidate.raw.ticksRan < model.cap &&
                                   candidate.raw.ticksRan < minSurviveLong;
            if (!truncated) {
                auto const cls =
                    ModeSurvival::classify(ShadowSimulatorImplementation::survivalInputFrom(
                        candidate.raw, zone, model, real));
                bool const longSafe = requireAlive ? ModeSurvival::countsAsDelayWorthy(cls)
                                                   : ModeSurvival::countsAsSafeForDelay(cls);
                if (!AssistHorizon::longPathSuccessOk(candidate.surviveTicks, candidate.raw.died,
                                                      longSafe, minSurviveLong)) {
                    candidate.viable = false;
                }
            }
        }
        // Linked dual: other player's shadow must also survive the same plan.
        if (candidate.viable && linkedDual()) {
            PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
            if (other && tickBudget > 0) {
                auto r2 =
                    runEngine(other, candidate.delay, releaseAfter, zone.valid ? &zone : nullptr);
                this->resetCaptureForNextRun(layer);
                if (!ShadowSimulatorImplementation::runResultDelaySafe(r2, zone, model, other,
                                                                       requireAlive)) {
                    candidate.viable = false;
                    candidate.clearsThreat = false;
                    candidate.dualFailed = true;
                }
            }
        }
    };

    {
        auto c0 = evalDelay(0, -1);
        plan.firstDeathTick = c0.raw.died ? c0.raw.deathTick : -1;
        plan.firstDeathObject = c0.raw.deathObject;
        if (c0.raw.died) {
            // Hybrid verify already clustered this zone — reuse (skip rebuild).
            if (options.seedThreatZone.valid) {
                zone = options.seedThreatZone;
            } else {
                zone = buildThreatZone(layer, c0.raw.deathX, c0.raw.deathObject);
            }
            plan.threatZone = zone;
        }
        finalizeViable(c0, -1);
        cands[0] = c0;
        candReady[0] = 1;

        // Continuous: idle (baseline coast) safer than d0 action → miss-assist may Delay.
        // For release, baseline = keep holding, d0 = UP now — same compare (enable always).
        plan.idleSafer = classifyIdleSafer(model.continuous, true, plan.baselineDeathTick >= 0,
                                           plan.baselineDeathTick, plan.baselineDeathX, c0.raw.died,
                                           c0.raw.died ? c0.raw.deathTick : -1,
                                           c0.raw.died ? c0.raw.deathX : c0.endX);

        // Live V2: LongForesight press-vs-idle over AssistHorizon (extend if press dies).
        if (!options.legacyPick) {
            int const h0 = std::max(LongForesight::kMinHorizon, horizonCaps.scoreHorizon);
            int const lookCap = StageConfig::lookAheadCap();
            int const hMax = lookCap > 0 ? std::max(h0, lookCap)
                                         : std::min(320, h0 + h0 / 2 + LongForesight::kExtendStep);
            int const minBudget = h0 * 2 + OpportunityProbe::kMinBudgetForProbe;

            auto pathFromRun = [](RunResult const& rr) {
                LongForesight::PathStats s;
                s.died = rr.died;
                s.deathTick = rr.died ? rr.deathTick : -1;
                s.endX = rr.died ? rr.deathX : rr.endX;
                s.surviveTicks = ShadowSimulatorImplementation::rankingSurviveTicks(rr);
                return s;
            };

            // Trajectory: lightweight hold/coast (same button semantics as traj viz).
            // ShadowPlayer: full runOne(d0) + runBaseline at long cap.
            auto sampleLight = [&](bool holdJump, int horizon) {
                LongForesight::PathStats s;
                if (!layer || !real || !m_shadow || horizon <= 0)
                    return s;
                copyPlayerState(m_shadow, real);
                m_dead = false;
                m_jumped = false;
                m_unknown = false;
                m_activatedRing = false;
                m_ringCount = 0;
                m_deathObject = nullptr;
                auto* sh = m_shadow;
                sh->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = holdJump;
                for (int tick = 0; tick < horizon; ++tick) {
                    if (tickBudget <= 0) {
                        plan.budgetHit = true;
                        break;
                    }
                    --tickBudget;
                    float const stepDt = (tick == 0) ? firstDt : deltaTime;
                    this->m_simAbsTick = this->m_simTickBase + tick;
                    stepPhysics(layer, sh, stepDt, deltaTime);
                    s.surviveTicks = tick + 1;
                    s.endX = sh->getPositionX();
                    if (m_dead) {
                        s.died = true;
                        s.deathTick = tick;
                        s.endX = sh->getPositionX();
                        break;
                    }
                    if (m_unknown) {
                        s.died = true;
                        s.deathTick = tick;
                        break;
                    }
                }
                this->resetCaptureForNextRun(layer);
                return s;
            };

            auto samplePair = [&](int horizon, LongForesight::PathStats& pressOut,
                                  LongForesight::PathStats& idleOut) {
                if (options.assistEngine == StageConfig::AssistEngineKind::Trajectory) {
                    // down=press → action holds Jump; idle coasts. Release flips.
                    pressOut = sampleLight(down, horizon);
                    idleOut = sampleLight(!down, horizon);
                    return;
                }
                Model longModel = model; // inherits continuePastClear
                longModel.cap = std::max(1, horizon);
                auto pressR = runOne(layer, real, down, 0, limit, deltaTime, firstDt, longModel, -1,
                                     zone.valid ? &zone : nullptr, &tickBudget);
                this->resetCaptureForNextRun(layer);
                auto idleR =
                    runBaseline(layer, real, down, deltaTime, firstDt, longModel, &tickBudget);
                this->resetCaptureForNextRun(layer);
                pressOut = pathFromRun(pressR);
                idleOut = pathFromRun(idleR);
            };

            if (tickBudget >= minBudget && !wallExceeded()) {
                LongForesight::Input lfIn;
                lfIn.horizon = h0;
                samplePair(h0, lfIn.press, lfIn.idle);

                int const h1 = LongForesight::extendHorizon(
                    h0, lfIn.press.died ? lfIn.press.deathTick : -1, hMax);
                if (h1 > h0 && tickBudget >= h1 * 2 && !wallExceeded()) {
                    lfIn.hasExtended = true;
                    lfIn.horizonExt = h1;
                    samplePair(h1, lfIn.pressExt, lfIn.idleExt);
                }

                auto const lf = LongForesight::decide(lfIn);
                // Only invent idleSafer / force search when idle still lives.
                if (lf.idleSafer)
                    plan.idleSafer = true;
                plan.longForesightNeedSearch = lf.needDelaySearch;
                AssistLog::debug(fmt::format(
                    "PLAN id={} longForesight eng={} h={} ext={} "
                    "pressDie={} idleDie={} idleSafer={} needSearch={} bothDead={} budget={}",
                    plan.planId, StageConfig::assistEngineName(options.assistEngine),
                    lf.horizonUsed, lf.extended ? 1 : 0, lf.pressSurvived ? 0 : 1,
                    lf.idleSurvived ? 0 : 1, lf.idleSafer ? 1 : 0, lf.needDelaySearch ? 1 : 0,
                    lf.bothDead ? 1 : 0, tickBudget));
            }
        }

        // No threat target: baseline survives and no orb/pad ahead.
        // G2: OpportunityProbe (foresight / mid+limit) may still force full search.
        bool const noThreat = !c0.raw.died && !zone.valid;
        bool const noOrbTarget = !(orbAssist && (orbHint.found || spatialHint || c0.activatedRing));
        if (noThreat && noOrbTarget) {
            OpportunityProbe::Sample baseSample{};
            baseSample.activatedRing = c0.activatedRing;
            baseSample.diedBeforeAction = c0.diedBeforeAction;
            baseSample.surviveBin = c0.surviveBin;
            baseSample.endX = c0.endX;

            OpportunityProbe::Sample probeSamples[2];
            int probeCount = 0;
            int const mid = OpportunityProbe::midDelay(limit);
            auto runProbeDelay = [&](int delay) {
                if (delay <= 0 || delay > limit)
                    return;
                if (candReady[static_cast<size_t>(delay)])
                    return;
                if (tickBudget < OpportunityProbe::kMinBudgetForProbe || wallExceeded()) {
                    plan.budgetHit = true;
                    return;
                }
                auto candidate = evalDelay(delay, -1);
                if (candidate.diedBeforeAction) {
                    // Still record — probe may show nothing useful.
                    finalizeViable(candidate, -1);
                    cands[static_cast<size_t>(delay)] = candidate;
                    candReady[static_cast<size_t>(delay)] = 1;
                    if (probeCount < 2) {
                        auto& ps = probeSamples[probeCount++];
                        ps.activatedRing = candidate.activatedRing;
                        ps.diedBeforeAction = candidate.diedBeforeAction;
                        ps.surviveBin = candidate.surviveBin;
                        ps.endX = candidate.endX;
                    }
                    return;
                }
                finalizeViable(candidate,
                               candidate.releaseAfter >= 0 ? candidate.releaseAfter : -1);
                cands[static_cast<size_t>(delay)] = candidate;
                candReady[static_cast<size_t>(delay)] = 1;
                if (probeCount < 2) {
                    auto& ps = probeSamples[probeCount++];
                    ps.activatedRing = candidate.activatedRing;
                    ps.diedBeforeAction = candidate.diedBeforeAction;
                    ps.surviveBin = candidate.surviveBin;
                    ps.endX = candidate.endX;
                }
            };

            if (limit >= 1) {
                runProbeDelay(mid);
                if (limit != mid) {
                    runProbeDelay(limit);
                }
            }

            bool searchFurther = OpportunityProbe::shouldSearchFurther(
                spatialHint, orbHint.found, c0.activatedRing, baseSample, probeSamples, probeCount);
            // Commit 11: trusted opportunity → MUST Delay-search (skip no-threat exit).
            if (options.mustDelaySearch)
                searchFurther = true;
            // Live V2 LongForesight: press dies on long horizon → keep searching.
            if (plan.longForesightNeedSearch)
                searchFurther = true;

            if (!searchFurther) {
                plan.action = PlanAction::Pass;
                // Wanted probes but could not afford any → PassBudget (C6 may Hold).
                // Commit 11: PassBudget only when probe budget is exhausted (allowPassBudget).
                if (plan.budgetHit && limit >= 1 && probeCount == 0 &&
                    PerfectAssistPolicy::allowPassBudget(true, options.mustDelaySearch, false)) {
                    plan.reason = AssistReason::PassBudget;
                } else {
                    plan.reason = AssistReason::PassNoThreat;
                }
                plan.delay = 0;
                plan.nowIsSafe = true;
                plan.threatCleared = true;
                plan.jumpTick = c0.jumpTick;
                plan.activatedRing = c0.activatedRing;
                AssistLog::debug(fmt::format(
                    "PLAN id={} Pass no-threat verdict={} foresight={} probes={} "
                    "budgetHit={} mustSearch={} limit={}",
                    plan.planId, AssistVerdict::name(plan.reason), plan.foresightOrbPad ? 1 : 0,
                    probeCount, plan.budgetHit, options.mustDelaySearch ? 1 : 0, limit));
                publishLedger(0, true,
                              plan.budgetHit ? DevRejectLedger::EarlyExit::Budget
                                             : DevRejectLedger::EarlyExit::NoThreat);
                return plan;
            }
            AssistLog::debug(fmt::format(
                "PLAN id={} opportunity-probe search-further foresight={} probes={} orbWin={}",
                plan.planId, plan.foresightOrbPad ? 1 : 0, probeCount, orbHint.found ? 1 : 0));
        }

        // Wave zigzag (press only): hazards in proximity ahead + no orb → Pass
        // even if d0 built a distant ThreatZone. Delaying phase-shifts into spikes.
        // Release Delay = hold longer — never Pass that path near hazards.
        // AFTER foresight: foresightOrbPad blocks this Pass (miss-assist orb).
        bool const isWave = real && real->m_isDart;
        float const proxX = std::clamp(static_cast<float>(limit + 4) * 5.5f, kHazardProximityXMin,
                                       kHazardProximityXMax);
        bool const nearHazard = isWave && spatialHazardProximity(layer, real, proxX);
        // Only suppress when d0 died (need to search for a living save).
        // idleSafer alone must not skip this Pass — Alive later is checked later.
        if (!c0.raw.died && shouldWaveHazardProximityPass(isWave, nearHazard, noOrbTarget,
                                                          plan.foresightOrbPad, down)) {
            plan.action = PlanAction::Pass;
            plan.reason = AssistReason::PassWaveProximity;
            plan.delay = 0;
            plan.nowIsSafe = c0.viable;
            plan.threatCleared = c0.clearsThreat || !zone.valid;
            plan.jumpTick = c0.jumpTick;
            plan.activatedRing = c0.activatedRing;
            plan.threatZone = zone;
            AssistLog::debug(fmt::format(
                "PLAN id={} Pass wave-hazard-proximity verdict={} prox={:.0f} zone={} budgetHit={}",
                plan.planId, AssistVerdict::name(plan.reason), proxX, zone.valid, plan.budgetHit));
            publishLedger(0, true, DevRejectLedger::EarlyExit::WaveProximity);
            return plan;
        }
    }

    std::vector<int> order;
    order.reserve(static_cast<size_t>(limit) + 1);
    order.push_back(0);
    auto pushUnique = [&](int delay) {
        if (delay < 0 || delay > limit)
            return;
        if (std::find(order.begin(), order.end(), delay) == order.end())
            order.push_back(delay);
    };
    if (orbHint.found) {
        for (int delay = orbHint.first; delay <= orbHint.last; ++delay)
            pushUnique(delay);
        for (int delay = std::max(0, orbHint.first - 2); delay <= std::min(limit, orbHint.last + 2);
             ++delay) {
            pushUnique(delay);
        }
    }

    // Bake-prior prefer band (after d0 / orb): evaluate ±k margin so robust check works.
    int preferLo = -1;
    int preferHi = -1;
    bool hasPrefer =
        options.preferDelayMin >= 0 && options.preferDelayMax >= options.preferDelayMin;
    if (hasPrefer) {
        preferLo = std::max(0, options.preferDelayMin);
        preferHi = std::min(limit, options.preferDelayMax);
        hasPrefer = preferLo <= preferHi;
    }
    int preferLastInOrder = -1;
    if (hasPrefer) {
        int const offset = robustK;
        int const marginLo = std::max(0, preferLo - offset);
        int const marginHi = std::min(limit, preferHi + offset);
        for (int delay = marginLo; delay <= marginHi; ++delay)
            pushUnique(delay);
        for (int delay : order) {
            if (delay >= marginLo && delay <= marginHi)
                preferLastInOrder = delay;
        }
    }

    for (int delay = 1; delay <= limit; ++delay)
        pushUnique(delay);

    auto preferBandRobust = [&]() -> bool {
        if (!hasPrefer)
            return false;
        int const offset = robustK;
        for (int delay = preferLo; delay <= preferHi; ++delay) {
            auto const& candidate = cands[static_cast<size_t>(delay)];
            if (!candidate.viable)
                continue;
            bool ok = true;
            for (int entry = delay - offset; entry <= delay + offset; ++entry) {
                if (entry < 0 || entry > limit)
                    continue;
                if (!cands[static_cast<size_t>(entry)].viable) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return true;
        }
        for (int delay = preferLo; delay <= preferHi; ++delay) {
            if (cands[static_cast<size_t>(delay)].viable)
                return true;
        }
        return false;
    };

    // Commit 10 PrefixSim: shared-prefix early-death + same-crash cut.
    int prefixDeathTick = -1;
    int sameCrashStreak = 0;
    int sameCrashPrevTick = -1;
    bool sameCrashStopped = false;

    bool const isWaveMode = real && real->m_isDart;
    bool const isShipLike =
        real && (real->m_isShip || real->m_isBird || real->m_isSpider || real->m_isSwing);
    bool const isRobot = real && real->m_isRobot;

    for (int delayValue : order) {
        if (delayValue == 0)
            continue;
        if (delayValue >= 0 && delayValue <= limit && candReady[static_cast<size_t>(delayValue)]) {
            continue; // already filled by OpportunityProbe
        }
        if (tickBudget <= 0 || wallExceeded()) {
            plan.budgetHit = true;
            // Remaining delays stay unevaluated → budget-not-reached in ledger.
            break;
        }

        // Shared prefix: idle death at T ⇒ skip every delay d > T (same fate).
        if (PrefixSim::shouldSkipAfterPrefixDeath(delayValue, prefixDeathTick)) {
            SimulationCandidate skip;
            skip.delay = delayValue;
            skip.diedBeforeAction = true;
            skip.raw.died = true;
            skip.raw.diedBeforeAction = true;
            skip.raw.deathTick = prefixDeathTick;
            finalizeViable(skip, -1);
            cands[static_cast<size_t>(delayValue)] = skip;
            candReady[static_cast<size_t>(delayValue)] = 1;
            continue;
        }

        auto candidate = evalDelay(delayValue, -1);
        if (candidate.diedBeforeAction) {
            prefixDeathTick =
                PrefixSim::mergePrefixDeathTick(prefixDeathTick, candidate.raw.deathTick, true);
            finalizeViable(candidate, -1);
            cands[static_cast<size_t>(delayValue)] = candidate;
            if (delayValue >= 0 && delayValue <= limit) {
                candReady[static_cast<size_t>(delayValue)] = 1;
            }
            // Fill remaining d > T without more sims.
            if (prefixDeathTick >= 0) {
                for (int sd : order) {
                    if (sd <= prefixDeathTick)
                        continue;
                    if (sd < 0 || sd > limit)
                        continue;
                    if (candReady[static_cast<size_t>(sd)])
                        continue;
                    SimulationCandidate fill;
                    fill.delay = sd;
                    fill.diedBeforeAction = true;
                    fill.raw.died = true;
                    fill.raw.diedBeforeAction = true;
                    fill.raw.deathTick = prefixDeathTick;
                    finalizeViable(fill, -1);
                    cands[static_cast<size_t>(sd)] = fill;
                    candReady[static_cast<size_t>(sd)] = 1;
                }
            }
            continue;
        }

        // Quick pass only here; hold-branch deferred to two-pass full-score set.
        finalizeViable(candidate, candidate.releaseAfter >= 0 ? candidate.releaseAfter : -1);
        cands[static_cast<size_t>(delayValue)] = candidate;
        if (delayValue >= 0 && delayValue <= limit) {
            candReady[static_cast<size_t>(delayValue)] = 1;
        }

        // Same deathTick after press across delays → next obstacle, stop.
        sameCrashStreak = PrefixSim::updateSameCrashStreak(
            sameCrashStreak, sameCrashPrevTick, candidate.raw.deathTick, candidate.raw.died,
            candidate.diedBeforeAction);
        if (candidate.raw.died && !candidate.diedBeforeAction && candidate.raw.deathTick >= 0) {
            sameCrashPrevTick = candidate.raw.deathTick;
        }
        if (PrefixSim::sameCrashShouldStop(sameCrashStreak)) {
            sameCrashStopped = true;
            AssistLog::debug(fmt::format("PLAN id={} same-crash stop deathTick={} after delay={}",
                                         plan.planId, candidate.raw.deathTick, delayValue));
            break;
        }

        // Early-exit once prefer±k margin is evaluated and a robust prefer delay exists.
        // v1 only (New choose removed): orb foresight still blocks exit so activations
        // outside prefer stay searchable.
        bool const preferExitOk = MissAssistContracts::preferBandEarlyExitAllowed(
            orbAssist, orbHint.found, spatialHint, plan.foresightOrbPad);
        if (hasPrefer && preferExitOk && !plan.budgetHit && delayValue == preferLastInOrder &&
            preferBandRobust()) {
            AssistLog::debug(fmt::format("PLAN id={} prefer-band early-exit [{},{}] after {} cands",
                                         plan.planId, preferLo, preferHi, plan.candidatesTried));
            ledgerPreferEarlyExit = true;
            break;
        }
        if (hasPrefer && !preferExitOk && delayValue == preferLastInOrder && preferBandRobust()) {
            AssistLog::debug(
                fmt::format("PLAN id={} prefer-band early-exit suppressed (orb foresight) [{},{}]",
                            plan.planId, preferLo, preferHi));
        }
    }

    // Commit 10 two-pass: expensive hold-branch / release theories only on full-score set.
    {
        std::vector<PrefixSim::QuickSample> quick;
        quick.reserve(static_cast<size_t>(limit) + 1);
        for (int delayValue = 0; delayValue <= limit; ++delayValue) {
            if (!candReady[static_cast<size_t>(delayValue)])
                continue;
            auto const& candidate = cands[static_cast<size_t>(delayValue)];
            PrefixSim::QuickSample s;
            s.delay = delayValue;
            s.died = candidate.raw.died;
            s.diedBeforeAction = candidate.diedBeforeAction;
            s.viable = candidate.viable;
            s.activatedRing = candidate.activatedRing;
            s.deathTick = candidate.raw.deathTick;
            s.surviveTicks = candidate.surviveTicks;
            s.endX = candidate.endX;
            quick.push_back(s);
        }
        auto const fullDelays = PrefixSim::delaysForFullScore(
            quick.data(), static_cast<int>(quick.size()), PrefixSim::kFullScoreTopK);
        bool const holdTheoryOk =
            PrefixSim::allowHoldBranchTheory(model.continuous, isRobot, isWaveMode, isShipLike);

        for (int delayValue : fullDelays) {
            if (delayValue < 0 || delayValue > limit)
                continue;
            if (tickBudget <= 0 || wallExceeded()) {
                plan.budgetHit = true;
                break;
            }
            auto& candidate = cands[static_cast<size_t>(delayValue)];

            // Mode-gated release branch (WaveTurn / ship recover / robot).
            if (candidate.raw.died && !candidate.diedBeforeAction && down && holdTheoryOk &&
                options.maxBranches > 0) {
                int tried = 0;
                for (int offset : ShadowSimulatorImplementation::BRANCH_RELEASES) {
                    if (tried >= options.maxBranches)
                        break;
                    if (tickBudget <= 0)
                        break;
                    ++tried;
                    auto br = runEngine(real, delayValue, offset, zone.valid ? &zone : nullptr);
                    this->resetCaptureForNextRun(layer);
                    if (!br.died || br.unknownTrunc) {
                        candidate.raw = br;
                        candidate.activatedRing = br.activatedRing;
                        candidate.unknownTrunc = br.unknownTrunc;
                        candidate.toggleUnknown = br.toggleUnknown;
                        if (candidate.toggleUnknown ||
                            (candidate.unknownTrunc && hadToggleTargets)) {
                            sawToggleUnknown = true;
                        }
                        candidate.endX = br.endX;
                        candidate.deathX = br.deathX;
                        candidate.surviveTicks =
                            ShadowSimulatorImplementation::rankingSurviveTicks(br);
                        candidate.surviveBin =
                            ShadowSimulatorImplementation::surviveBinOf(candidate.surviveTicks);
                        candidate.releaseAfter = offset;
                        candidate.requireHold = false;
                        finalizeViable(candidate, offset);
                        break;
                    }
                }
            }

            // Dash hold salvage (still two-pass gated to full-score set).
            if (candidate.raw.activatedRing && candidate.raw.wasDashing && !candidate.viable &&
                tickBudget > 0) {
                auto hold = runEngine(real, candidate.delay, 40, zone.valid ? &zone : nullptr);
                this->resetCaptureForNextRun(layer);
                SimulationCandidate height = candidate;
                height.raw = hold;
                height.activatedRing = hold.activatedRing;
                height.unknownTrunc = hold.unknownTrunc;
                height.toggleUnknown = hold.toggleUnknown;
                if (height.toggleUnknown || (height.unknownTrunc && hadToggleTargets)) {
                    sawToggleUnknown = true;
                }
                height.endX = hold.endX;
                height.deathX = hold.deathX;
                height.surviveTicks = ShadowSimulatorImplementation::rankingSurviveTicks(hold);
                height.surviveBin =
                    ShadowSimulatorImplementation::surviveBinOf(height.surviveTicks);
                finalizeViable(height, 40);
                if (height.viable && !candidate.viable) {
                    candidate = height;
                    candidate.requireHold = true;
                    candidate.releaseAfter = 40;
                }
            }
        }

        if (sameCrashStopped) {
            AssistLog::debug(fmt::format("PLAN id={} prefixSim sameCrash={} prefixDeathTick={}",
                                         plan.planId, 1, prefixDeathTick));
        }
    }

    bool anyActivation = false;
    for (auto const& candidate : cands) {
        if (candidate.activatedRing)
            anyActivation = true;
    }
    bool orbIntent = orbAssist && (orbHint.found || spatialHint || anyActivation);
    ledgerOrbIntent = orbIntent;

    // Continuous ascentUseful: later Delay-worthy Alive beats d0 (not soft-dead).
    {
        auto const& c0ref = cands[0];
        for (int delay = 1; delay <= limit; ++delay) {
            auto const& candidate = cands[static_cast<size_t>(delay)];
            // Enable for press and release (hold-longer can beat immediate UP).
            if (classifyAscentUseful(model.continuous, true, c0ref.viable, c0ref.surviveBin,
                                     c0ref.endX, candidate.viable, candidate.surviveBin,
                                     candidate.endX, !candidate.raw.died)) {
                plan.ascentUseful = true;
                break;
            }
        }
    }

    // Wave: no Alive delay improves surviveBin / endX → Pass (avoid phase-shift).
    // Never discard a living delay when d0 died. idleSafer alone does not skip
    // this Pass — need an Alive later cand.
    {
        bool const isWave = real && real->m_isDart;
        int bestBin = 0;
        float bestGain = 0.f;
        float const baseEnd = cands[0].endX;
        bool anySavesFromD0Death = false;
        bool anyAliveLater = false;
        for (int delay = 0; delay <= limit; ++delay) {
            auto const& candidate = cands[static_cast<size_t>(delay)];
            bool const aliveWorthy = candidate.viable && !candidate.raw.died;
            if (!aliveWorthy)
                continue;
            if (candidate.surviveBin > bestBin)
                bestBin = candidate.surviveBin;
            float const gain = candidate.endX - baseEnd;
            if (gain > bestGain)
                bestGain = gain;
            if (delay > 0) {
                anyAliveLater = true;
                if (cands[0].raw.died)
                    anySavesFromD0Death = true;
            }
        }
        // idleSafer skips futile Pass only when an Alive later escape exists.
        bool const idleHoldsEscape = plan.idleSafer && anyAliveLater;
        if (!anySavesFromD0Death && !idleHoldsEscape &&
            shouldWaveFutileDelayPass(isWave, orbIntent, bestBin, bestGain, plan.foresightOrbPad,
                                      down)) {
            plan.action = PlanAction::Pass;
            plan.reason = AssistReason::PassWaveFutile;
            plan.delay = 0;
            plan.nowIsSafe = cands[0].viable;
            plan.threatCleared = cands[0].clearsThreat || !zone.valid;
            plan.jumpTick = cands[0].jumpTick;
            plan.activatedRing = cands[0].activatedRing;
            plan.threatZone = zone;
            AssistLog::debug(fmt::format(
                "PLAN id={} Pass wave-futile-delay verdict={} bin={} gain={:.1f} budgetHit={}",
                plan.planId, AssistVerdict::name(plan.reason), bestBin, bestGain, plan.budgetHit));
            publishLedger(0, true, DevRejectLedger::EarlyExit::WaveFutile);
            return plan;
        }
    }

    for (auto& candidate : cands) {
        candidate.orbUseful = false;
        if (!orbAssist || !candidate.activatedRing || !candidate.viable)
            continue;
        // Viable activation is useful for ranking — do not require spatialHint
        // when d0 already "safe" but missed the ring (orb-miss G1).
        if (!cands[0].clearsThreat) {
            candidate.orbUseful = true;
            continue;
        }
        if (!cands[0].activatedRing) {
            candidate.orbUseful = true;
            continue;
        }
        candidate.orbUseful = true;
    }

    // Optional: cand must also clear threat under firstDt ±3%.
    // Commit 10: jitter only Top-2 ranked picks (never walk the whole pool).
    auto jitterOk = [&](int delay, int releaseAfter) -> bool {
        if (!options.robustJitter || delay < 0)
            return true;
        if (tickBudget <= 0 || wallExceeded())
            return true; // soft accept
        static float const kScales[] = {0.97f, 1.03f};
        for (float state : kScales) {
            float jd = firstDt * state;
            if (jd <= 0.f || jd > deltaTime * 1.5f)
                jd = firstDt;
            RunResult result;
            if (useTrajEngine) {
                result = runTrajectoryOne(layer, real, down, delay, limit, deltaTime, jd, model,
                                          releaseAfter, zone.valid ? &zone : nullptr, &tickBudget);
            } else {
                result = runOne(layer, real, down, delay, limit, deltaTime, jd, model, releaseAfter,
                                zone.valid ? &zone : nullptr, &tickBudget);
            }
            this->resetCaptureForNextRun(layer);
            if (!ShadowSimulatorImplementation::clearsThreatOf(result, zone) ||
                result.diedBeforeAction)
                return false;
        }
        return true;
    };
    // pickFn returns next best delay; jitter capped at PrefixSim::kJitterTopK attempts.
    auto pickWithJitterTop2 = [&](auto&& pickFn) -> int {
        int attempts = 0;
        for (;;) {
            int const pick = pickFn();
            if (pick < 0)
                return -1;
            int const releaseAfter = cands[static_cast<size_t>(pick)].requireHold ? 40 : -1;
            if (!options.robustJitter)
                return pick;
            if (!PrefixSim::jitterAttemptAllowed(attempts, PrefixSim::kJitterTopK)) {
                // Top-2 already failed jitter — stop (do not burn the pool).
                return -1;
            }
            ++attempts;
            if (jitterOk(pick, releaseAfter))
                return pick;
            cands[static_cast<size_t>(pick)].viable = false;
            cands[static_cast<size_t>(pick)].robustKFailed = true;
        }
    };

    int actMin = -1, actMax = -1;
    for (int delay = 0; delay <= limit; ++delay) {
        auto const& candidate = cands[static_cast<size_t>(delay)];
        if (candidate.viable && candidate.activatedRing) {
            if (actMin < 0)
                actMin = delay;
            actMax = delay;
        }
    }
    // Window-cap: activation band never reported past limit.
    if (actMin > limit)
        actMin = limit;
    if (actMax > limit)
        actMax = limit;
    plan.dMin = actMin;
    plan.dMax = actMax;
    ledgerActMin = actMin;
    ledgerActMax = actMax;

    // Live choose: v1 = AssistPickV1 (latest-robust); v2 = AssistPickV2 (foresight-first).
    bool const usePickV2 = !options.legacyPick;
    std::vector<AssistPickV1::CandView> pickViewsV1(static_cast<size_t>(std::max(0, limit)) + 1u);
    std::vector<AssistPickV2::CandView> pickViewsV2(static_cast<size_t>(std::max(0, limit)) + 1u);
    auto syncPickViews = [&]() {
        for (int delay = 0; delay <= limit; ++delay) {
            auto const& candidate = cands[static_cast<size_t>(delay)];
            if (usePickV2) {
                auto& v = pickViewsV2[static_cast<size_t>(delay)];
                v.viable = candidate.viable;
                v.activatedRing = candidate.activatedRing;
                v.orbUseful = candidate.orbUseful;
                v.surviveBin = candidate.surviveBin;
                v.surviveTicks = candidate.surviveTicks;
                v.endX = candidate.endX;
                v.died = candidate.raw.died;
            } else {
                auto& v = pickViewsV1[static_cast<size_t>(delay)];
                v.viable = candidate.viable;
                v.activatedRing = candidate.activatedRing;
            }
        }
    };
    auto pickEarliestRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
        syncPickViews();
        if (usePickV2) {
            return AssistPickV2::pickEarliestRobust(pickViewsV2.data(), limit, robustK, pool,
                                                    needActivation);
        }
        return AssistPickV1::pickEarliestRobust(pickViewsV1.data(), limit, robustK, pool,
                                                needActivation);
    };
    auto pickLatestRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
        syncPickViews();
        if (usePickV2) {
            return AssistPickV2::pickLatestRobust(pickViewsV2.data(), limit, robustK, pool,
                                                  needActivation);
        }
        return AssistPickV1::pickLatestRobust(pickViewsV1.data(), limit, robustK, pool,
                                              needActivation);
    };
    auto applyChosenMeta = [&](int pick) {
        if (pick < 0 || pick > limit)
            return;
        auto const& candidate = cands[static_cast<size_t>(pick)];
        plan.threatCleared = candidate.clearsThreat;
        plan.activatedRing = candidate.activatedRing;
        plan.requireHold = candidate.requireHold;
        plan.orbUseful = candidate.orbUseful;
    };

    int chosen = -1;
    bool doPass = false;
    bool haveActViable = actMin >= 0;

    if (orbIntent && haveActViable) {
        std::vector<int> pool;
        for (int delay = actMin; delay <= actMax; ++delay)
            pool.push_back(delay);
        int const offset = robustK;
        auto tryPick = [&](std::vector<int> const& p) -> int {
            return pickWithJitterTop2([&]() { return pickEarliestRobust(p, true); });
        };
        if (actMax - actMin >= 2 * offset) {
            std::vector<int> interior;
            for (int delay = actMin + offset; delay <= actMax - offset; ++delay)
                interior.push_back(delay);
            chosen = tryPick(interior);
        }
        if (chosen < 0)
            chosen = tryPick(pool);
        if (chosen >= 0) {
            applyChosenMeta(chosen);
            plan.requireOrb = true;
        }
    } else if (usePickV2) {
        // V2 foresight-first (Hybrid bake wins never reach this Live path).
        AssistPickV2::Context ctx;
        ctx.idleSafer = plan.idleSafer;
        ctx.ascentUseful = plan.ascentUseful;
        ctx.longForesightNeedSearch = plan.longForesightNeedSearch;
        ctx.latencyWeight = latencyWeight;

        auto runV2Choose = [&]() -> AssistPickV2::Result {
            syncPickViews();
            return AssistPickV2::choose(pickViewsV2.data(), limit, robustK, false, -1, -1, ctx);
        };

        // Jitter top-2 around foresight pick; then re-choose on surviving cands.
        chosen = pickWithJitterTop2([&]() { return runV2Choose().chosen; });
        {
            auto result = runV2Choose();
            if (result.chosen >= 0) {
                chosen = result.chosen;
                doPass = result.doPass;
            } else if (chosen >= 0 && chosen <= limit &&
                       cands[static_cast<size_t>(chosen)].viable) {
                doPass = (chosen == 0);
            } else {
                chosen = -1;
                doPass = false;
            }
            if (chosen >= 0)
                applyChosenMeta(chosen);
            if (doPass && chosen == 0) {
                plan.threatCleared = true;
                plan.nowIsSafe = true;
            }
        }
        AssistLog::debug(fmt::format(
            "PLAN id={} pickV2 foresight chosen={} pass={} idleSafer={} lfSearch={} ascent={}",
            plan.planId, chosen, doPass ? 1 : 0, plan.idleSafer ? 1 : 0,
            plan.longForesightNeedSearch ? 1 : 0, plan.ascentUseful ? 1 : 0));
    } else {
        // v1: d0 viable → Pass. Else latest ±k-robust in window (miss-assist wait).
        if (cands[0].viable) {
            chosen = pickWithJitterTop2([&]() { return cands[0].viable ? 0 : -1; });
            if (chosen == 0) {
                doPass = true;
                plan.threatCleared = true;
                plan.nowIsSafe = true;
            } else {
                std::vector<int> pool;
                for (int delay = 0; delay <= limit; ++delay)
                    pool.push_back(delay);
                chosen = pickWithJitterTop2([&]() { return pickLatestRobust(pool, false); });
                applyChosenMeta(chosen);
            }
        } else {
            std::vector<int> pool;
            for (int delay = 0; delay <= limit; ++delay)
                pool.push_back(delay);
            chosen = pickWithJitterTop2([&]() { return pickLatestRobust(pool, false); });
            applyChosenMeta(chosen);
        }
    }

    // G1 window-cap assert.
    if (chosen > limit)
        chosen = limit;
    // Hard gate: never arm a non-viable Delay (Commit 9: Landing/Recoverable may
    // have raw.died but still be Delay-safe via ModeSurvival).
    if (chosen > 0 && chosen <= limit) {
        auto const& ch = cands[static_cast<size_t>(chosen)];
        if (!ch.viable || ch.diedBeforeAction || ch.unknownTrunc || ch.toggleUnknown) {
            if (ch.toggleUnknown || (ch.unknownTrunc && hadToggleTargets)) {
                sawToggleUnknown = true;
            }
            AssistLog::debug(
                fmt::format("PLAN id={} reject lethal/unknown Delay +{} → Pass toggleUnk={}",
                            plan.planId, chosen, sawToggleUnknown ? 1 : 0));
            chosen = -1;
            doPass = false;
            plan.requireOrb = false;
        }
    }
    if (chosen < 0) {
        // Commit 11: never soft-trust budgetHit into Delay via partial actMin.
        if (plan.budgetHit && actMin >= 0 && actMin <= limit &&
            cands[static_cast<size_t>(actMin)].viable &&
            PerfectAssistPolicy::allowArmDelay(true, plan.budgetHit, /*softTrustPartial=*/true)) {
            chosen = actMin;
            doPass = false;
            applyChosenMeta(chosen);
            plan.requireOrb = true;
            AssistLog::debug(fmt::format("PLAN id={} budget-partial actMin={} pick={}", plan.planId,
                                         actMin, chosen));
        }
    }

    // Continuous miss-assist: idle safer + ascent → Delay only with Alive later.
    // Never force Delay on soft-dead. InterveneGate: ascentUseful alone is NOT
    // "improves" when d0 is already Delay-worthy Alive (help-don't-hurt).
    if (plan.idleSafer && plan.ascentUseful && (doPass || chosen <= 0)) {
        std::vector<int> pool;
        for (int delay = 1; delay <= limit; ++delay)
            pool.push_back(delay);
        int forced = pickLatestRobust(pool, false);
        if (forced > 0) {
            auto const& fc = cands[static_cast<size_t>(forced)];
            bool const forcedWorthy = fc.viable && !fc.raw.died;
            if (forcedWorthy) {
                bool const d0Ok = cands[0].viable && !cands[0].raw.died;
                bool improves = !d0Ok;
                if (d0Ok) {
                    improves = fc.surviveBin > cands[0].surviveBin ||
                               fc.endX > cands[0].endX + kFutileEndXGain;
                }
                if (PerfectAssistPolicy::interveneOk(d0Ok, true, improves)) {
                    chosen = forced;
                    doPass = false;
                    applyChosenMeta(chosen);
                    AssistLog::debug(
                        fmt::format("PLAN id={} idleSafer+ascent → Delay +{} (was Pass/hopeless)",
                                    plan.planId, chosen));
                }
            } else {
                AssistLog::debug(fmt::format("PLAN id={} idleSafer+ascent skip — no Alive later "
                                             "(forced={} viable={} died={})",
                                             plan.planId, forced, fc.viable ? 1 : 0,
                                             fc.raw.died ? 1 : 0));
            }
        }
    }
    // Re-assert Delay-worthy gate after idleSafer / budget-partial.
    if (chosen > 0 && chosen <= limit) {
        auto const& ch = cands[static_cast<size_t>(chosen)];
        if (!ch.viable || ch.raw.died || ch.diedBeforeAction || ch.unknownTrunc ||
            ch.toggleUnknown) {
            if (ch.toggleUnknown || (ch.unknownTrunc && hadToggleTargets)) {
                sawToggleUnknown = true;
            }
            chosen = -1;
            doPass = false;
            plan.requireOrb = false;
        }
    }

    // assist-verify=ab formerly coin-flipped Legacy vs New; now logs active pick.
    if (StageConfig::assistVerifyAb()) {
        AssistLog::debug(fmt::format("VERIFY id={} ab-noop pick={} chosen={} doPass={}",
                                     plan.planId, usePickV2 ? "v2" : "v1", chosen, doPass ? 1 : 0));
    }

    // Hold/release window for the chosen Delay: how long can they hold, when
    // to let go without dying. Covers ship/wave hold, robot, orb dash holds.
    if (chosen > 0 && down && tickBudget > 0 && !wallExceeded()) {
        auto const& base = cands[static_cast<size_t>(chosen)];
        int earliest = -1;
        int latest = -1;
        int bestRel = base.releaseAfter;
        int bestBin = base.surviveBin;
        float bestEnd = base.endX;
        int tried = 0;

        // Seed from branch that already won.
        if (base.releaseAfter >= 0 && base.viable) {
            earliest = base.releaseAfter;
            latest = base.releaseAfter;
        }

        for (int rel : ShadowSimulatorImplementation::HOLD_PROBE_RELEASES) {
            if (tried >= ShadowSimulatorImplementation::HOLD_PROBE_MAX)
                break;
            if (tickBudget <= 0 || wallExceeded())
                break;
            if (base.releaseAfter >= 0 && rel == base.releaseAfter)
                continue;
            ++tried;
            auto result = runEngine(real, chosen, rel, zone.valid ? &zone : nullptr);
            this->resetCaptureForNextRun(layer);
            ++plan.candidatesTried;

            // Hold branch — same Delay-viable rule (soft bake vs Live Alive-only).
            if (!ShadowSimulatorImplementation::runResultDelaySafe(result, zone, model, real,
                                                                   requireAlive))
                continue;

            int const sTicks = ShadowSimulatorImplementation::rankingSurviveTicks(result);
            int const sBin = ShadowSimulatorImplementation::surviveBinOf(sTicks);
            if (earliest < 0 || rel < earliest)
                earliest = rel;
            if (latest < 0 || rel > latest)
                latest = rel;
            if (bestRel < 0 || sBin > bestBin ||
                (sBin == bestBin && result.endX > bestEnd + 0.5f) ||
                (sBin == bestBin && std::fabs(result.endX - bestEnd) <= 0.5f &&
                 (model.continuous ? rel < bestRel : false))) {
                bestRel = rel;
                bestBin = sBin;
                bestEnd = result.endX;
            }
        }

        plan.holdScanTried = tried;
        if (earliest >= 0) {
            plan.releaseEarliest = earliest;
            plan.releaseLatest = latest >= earliest ? latest : earliest;
            if (bestRel < 0) {
                bestRel = pickPreferredRelease(plan.releaseEarliest, plan.releaseLatest,
                                               model.continuous);
            }
            plan.releaseAfter = bestRel;
            if (plan.releaseAfter >= 40 || base.requireHold) {
                plan.requireHold = true;
            }
        } else if (base.releaseAfter >= 0) {
            plan.releaseAfter = base.releaseAfter;
            plan.releaseEarliest = base.releaseAfter;
            plan.releaseLatest = base.releaseAfter;
        }
    }

    if (chosen < 0) {
        plan.action = PlanAction::Pass;
        plan.toggleUnknown = sawToggleUnknown;
        plan.unknown = sawToggleUnknown;
        plan.reason = sawToggleUnknown ? AssistReason::PassToggleUnknown
                                       : (plan.budgetHit ? AssistReason::PassBudget
                                                         : AssistReason::PassHopeless);
        plan.delay = 0;
        AssistLog::debug(fmt::format(
            "PLAN id={} Pass verdict={} limit={} budgetHit={} toggleUnk={} orbIntent={}",
            plan.planId, AssistVerdict::name(plan.reason), limit, plan.budgetHit,
            sawToggleUnknown ? 1 : 0, orbIntent));
        DevRejectLedger::EarlyExit early = DevRejectLedger::EarlyExit::None;
        if (plan.budgetHit)
            early = DevRejectLedger::EarlyExit::Budget;
        else if (ledgerPreferEarlyExit)
            early = DevRejectLedger::EarlyExit::PreferBand;
        publishLedger(0, true, early);
        return plan;
    }

    if (doPass || chosen == 0) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassD0Safe;
        plan.delay = 0;
        plan.nowIsSafe = cands[0].viable;
        plan.jumpTick = cands[0].jumpTick;
        plan.activatedRing = cands[0].activatedRing;
        plan.toggleUnknown = sawToggleUnknown || cands[0].toggleUnknown;
        plan.unknown = plan.toggleUnknown || cands[0].unknownTrunc;
        // d0 "safe" that only looked clear through unknown → honesty Pass.
        if (cands[0].unknownTrunc || cands[0].toggleUnknown || !cands[0].viable) {
            if (sawToggleUnknown || cands[0].toggleUnknown || hadToggleTargets) {
                plan.reason = AssistReason::PassToggleUnknown;
                plan.toggleUnknown = true;
                plan.unknown = true;
            } else if (cands[0].unknownTrunc) {
                // Commit 11: Hard Unknown leftover → named Pass (never Delay).
                plan.reason = AssistReason::PassHardUnknown;
                plan.unknown = true;
            }
        }
    } else {
        auto const& winCand = cands[static_cast<size_t>(chosen)];
        // Belt: never ship Delay on unknown/toggle-unknown path.
        if (winCand.unknownTrunc || winCand.toggleUnknown) {
            plan.action = PlanAction::Pass;
            plan.delay = 0;
            plan.unknown = true;
            plan.nowIsSafe = false;
            if (winCand.toggleUnknown || hadToggleTargets) {
                plan.reason = AssistReason::PassToggleUnknown;
                plan.toggleUnknown = true;
            } else {
                // Commit 11: Hard Unknown → pass-hard-unknown.
                plan.reason = AssistReason::PassHardUnknown;
                plan.toggleUnknown = false;
            }
        } else {
            plan.action = PlanAction::Delay;
            plan.reason = AssistVerdict::inferDelayReason(
                plan.activatedRing || winCand.activatedRing, plan.requireOrb, hasPrefer);
            plan.delay = chosen;
            plan.jumpTick = winCand.jumpTick;
            plan.nowIsSafe = false;
            plan.activatedRing = plan.activatedRing || winCand.activatedRing;
            // Chosen path is clean — other cands may have hit toggle-unknown.
            plan.unknown = false;
            plan.toggleUnknown = false;
            if (plan.releaseAfter < 0 && winCand.releaseAfter >= 0) {
                plan.releaseAfter = winCand.releaseAfter;
                plan.releaseEarliest = plan.releaseAfter;
                plan.releaseLatest = plan.releaseAfter;
            }
            plan.requireHold = plan.requireHold || winCand.requireHold;
            // Non-orb: publish survivor band for overlay / prefer.
            if (!plan.requireOrb) {
                int sLo = -1;
                int sHi = -1;
                for (int delay = 0; delay <= limit; ++delay) {
                    auto const& candidate = cands[static_cast<size_t>(delay)];
                    if (!candidate.viable)
                        continue; // Commit 9: Landing may set raw.died
                    if (sLo < 0)
                        sLo = delay;
                    sHi = delay;
                }
                if (sLo >= 0) {
                    plan.dMin = sLo;
                    plan.dMax = sHi;
                }
            }
        }
    }

    // Commit 8: exact viable delays in window (exit-predict must not fill gaps).
    {
        plan.viableDelays.clear();
        plan.viableDelays.reserve(static_cast<size_t>(limit) + 1);
        for (int delay = 0; delay <= limit; ++delay) {
            auto const& candidate = cands[static_cast<size_t>(delay)];
            if (!candidate.viable)
                continue;
            plan.viableDelays.push_back(delay);
        }
    }

    // Dash orbs: sim keeps Jump down via tapRelease exceptions, but live inject
    // used to default releaseAfter=1 and cancel the dash. Force hold when the
    // winning Delay cand dashed.
    if (plan.action == PlanAction::Delay && chosen >= 0 && chosen <= limit) {
        auto const& win = cands[static_cast<size_t>(chosen)];
        if (win.raw.wasDashing) {
            plan.requireHold = true;
            plan.activatedRing = true;
            if (plan.releaseAfter < 40)
                plan.releaseAfter = 40;
            if (plan.releaseEarliest < 0 || plan.releaseEarliest > plan.releaseAfter) {
                plan.releaseEarliest = plan.releaseAfter;
            }
            if (plan.releaseLatest < 120)
                plan.releaseLatest = 120;
        }
    }

    // Final honesty: never ship Delay when the *chosen* path is unknown.
    if (plan.action == PlanAction::Delay && (plan.unknown || plan.toggleUnknown)) {
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.unknown = true;
        plan.requireHold = false;
        if (plan.toggleUnknown) {
            plan.reason = AssistReason::PassToggleUnknown;
            AssistLog::debug(
                fmt::format("PLAN id={} refuse Delay → pass-toggle-unknown", plan.planId));
        } else {
            // Commit 11: Hard Unknown → named Pass.
            plan.reason = AssistReason::PassHardUnknown;
            AssistLog::debug(
                fmt::format("PLAN id={} refuse Delay → pass-hard-unknown", plan.planId));
        }
    }

    // Commit 11: fully evaluated Delay stays armed even if later search hit budget.
    // Soft-trust partial pick is blocked above (budget-partial + softTrustPartial).

    if (plan.action == PlanAction::Delay && plan.reason == AssistReason::None) {
        plan.reason =
            AssistVerdict::inferDelayReason(plan.activatedRing, plan.requireOrb, hasPrefer);
    }
    if (plan.action == PlanAction::Pass && plan.reason == AssistReason::None) {
        plan.reason = AssistReason::PassUnknown;
    }

    bool const orbIntentOut = plan.requireOrb || plan.activatedRing || plan.usedOrbWindow;
    AssistLog::debug(fmt::format(
        "PLAN id={} {} verdict={} delay={} orbIntent={} foresight={} idleSafer={} ascent={} eng={} "
        "ring={} useful={} hold={} rel=[{},{}]@{} scan={} dMin={} dMax={} tried={} budgetLeft={}",
        plan.planId, plan.action == PlanAction::Pass ? "Pass" : "Delay",
        AssistVerdict::name(plan.reason), plan.delay, orbIntentOut ? 1 : 0,
        plan.foresightOrbPad ? 1 : 0, plan.idleSafer ? 1 : 0, plan.ascentUseful ? 1 : 0,
        plan.trajectoryEngine ? "Trajectory" : "ShadowPlayer", plan.activatedRing, plan.orbUseful,
        plan.requireHold, plan.releaseEarliest, plan.releaseLatest, plan.releaseAfter,
        plan.holdScanTried, plan.dMin, plan.dMax, plan.candidatesTried, tickBudget));
    {
        DevRejectLedger::EarlyExit early = DevRejectLedger::EarlyExit::None;
        if (plan.budgetHit)
            early = DevRejectLedger::EarlyExit::Budget;
        else if (ledgerPreferEarlyExit)
            early = DevRejectLedger::EarlyExit::PreferBand;
        bool const pass = (plan.action == PlanAction::Pass);
        publishLedger(pass ? 0 : plan.delay, pass, early);
    }
    return plan;
}

SimPlan ShadowSimulator::planSequence(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                                      float deltaTime, float firstDt, PlanOptions const& options) {
    auto single = plan(layer, real, down, limit, deltaTime, firstDt, options);
    int const depth = std::clamp(options.smartDepth, 1, 3);

    // Smart multi-jump / bake-chain only when smartDepth > 1.
    // Former New default (exit-predict at depth 1) removed with New choose.
    std::vector<int> allowed;
    if (single.action == PlanAction::Pass) {
        allowed.push_back(0);
    } else if (!single.viableDelays.empty()) {
        for (int delayValue : single.viableDelays) {
            if (delayValue < 0 || delayValue > limit)
                continue;
            allowed.push_back(delayValue);
        }
        if (allowed.empty())
            allowed.push_back(single.delay);
    } else {
        // Fallback when plan skipped cand eval (rare): only the chosen delay.
        allowed.push_back(std::clamp(single.delay, 0, limit));
    }
    if (depth <= 1 || !down) {
        single.smartDepthUsed = 1;
        return single;
    }

    Model model = modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    // ScoreHorizon from AssistHorizon (long only when trusted).
    int horizon = options.smartHorizon;
    if (horizon <= 0) {
        bool const trustOk = SimTrust::get().okAt(real->getPositionX());
        horizon = AssistHorizon::resolve(StageConfig::kindOf(real), trustOk, limit, options.capMul,
                                         0, StageConfig::lookAheadCap())
                      .scoreHorizon;
        horizon = std::max(horizon, model.cap);
    }
    horizon = std::clamp(horizon, 32, 320);
    int comboBudget = std::clamp(options.smartCombos, 8, 256);

    int slot = captureSnapshot(real);
    if (slot < 0)
        return single;

    class Best {
    public:
        bool found = false;
        int d0 = 0;
        int score = -1;
        bool survived = false;

    private:
    } best;

    auto pushTap = [&](std::vector<InputEvent>& tl, int pressTick, bool tap) {
        tl.push_back(InputEvent{pressTick, true});
        if (tap)
            tl.push_back(InputEvent{pressTick + 2, false});
    };
    bool const tap = model.tapRelease;
    int minGap = tap ? 6 : 4;
    int step = std::max(2, horizon / 40);
    int combos = 0;

    // Smart bake-chain: fix follow-up clicks from BakeMap when available.
    GameModeKind const kind = StageConfig::kindOf(real);
    bool const player2 = layer && layer->m_player2 && real == layer->m_player2;
    int const chainDepth = std::max(0, depth - 1);
    auto followUps = (BakeMap::get().loaded() && chainDepth > 0)
                         ? BakeMap::get().nextClicksAfter(real->getPositionX(), true, player2, kind,
                                                          std::min(chainDepth, 2))
                         : std::vector<BakedClick const*>{};

    float const speed =
        std::max(0.5f, real->m_playerSpeed * 311.2f * std::max(deltaTime, 1.f / 240.f));
    std::vector<BakedClick const*> chain;
    for (auto const* candidate : followUps) {
        if (!candidate)
            continue;
        if (candidate->mode != kind)
            break;
        float const deltaX = candidate->x - real->getPositionX();
        int const eta = static_cast<int>(std::lround(deltaX / speed));
        if (eta <= 0 || eta >= horizon)
            continue;
        chain.push_back(candidate);
        if (static_cast<int>(chain.size()) >= chainDepth)
            break;
    }
    bool const useBakeChain = !chain.empty();

    for (int d0 : allowed) {
        if (combos >= comboBudget)
            break;

        if (useBakeChain) {
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            for (auto const* candidate : chain) {
                float const deltaX = candidate->x - real->getPositionX();
                int tFollow = std::max(d0 + minGap, static_cast<int>(std::lround(deltaX / speed)));
                int const midFrame = (candidate->earliest + candidate->latest) / 2;
                int const approxNow = options.attemptStartTick;
                if (midFrame > approxNow) {
                    tFollow = std::max(d0 + minGap, midFrame - approxNow);
                }
                if (tFollow >= horizon)
                    continue;
                pushTap(tl, tFollow, tap);
            }
            auto rr = replay(layer, slot, 0, tl, deltaTime, horizon, false, 0);
            ++combos;
            bool const survived =
                ShadowSimulatorImplementation::exitSurvivedHonest(rr.died, rr.unknown);
            int sc = survived ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing)
                sc += 5000;
            if (!chain.empty() && survived) {
                float const targetX = chain.front()->x;
                if (rr.endX + BakeMap::LOOKUP_X_TOL >= targetX)
                    sc += 20000;
                else
                    sc -= 10000;
            }
            if (!best.found || sc > best.score) {
                best = {true, d0, sc, survived};
            }
            continue;
        }

        {
            // Single-press exit probe: which first delay actually clears ahead?
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            auto rr = replay(layer, slot, 0, tl, deltaTime, horizon, false, 0);
            ++combos;
            bool const survived =
                ShadowSimulatorImplementation::exitSurvivedHonest(rr.died, rr.unknown);
            int sc = survived ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing)
                sc += 5000;
            if (!best.found || sc > best.score) {
                best = {true, d0, sc, survived};
            }
        }
        if (depth >= 2) {
            for (int d1 = d0 + minGap; d1 < horizon && combos < comboBudget; d1 += step) {
                std::vector<InputEvent> tl2;
                pushTap(tl2, d0, tap);
                pushTap(tl2, d1, tap);
                auto rr2 = replay(layer, slot, 0, tl2, deltaTime, horizon, false, 0);
                ++combos;
                bool const survived2 =
                    ShadowSimulatorImplementation::exitSurvivedHonest(rr2.died, rr2.unknown);
                int sc2 = survived2 ? 100000 : rr2.deathTick * 50;
                float const lat2 = std::max(0.f, options.latencyWeight);
                sc2 += static_cast<int>(rr2.endX - lat2 * static_cast<float>(d0));
                if (rr2.activatedRing)
                    sc2 += 5000;
                if (!best.found || sc2 > best.score) {
                    best = {true, d0, sc2, survived2};
                }
            }
        }
    }

    single.smartDepthUsed = depth;
    single.smartCombosUsed = combos;
    // Only accept a predicted exit that actually survived the horizon.
    if (!best.found || !best.survived) {
        AssistLog::debug(fmt::format("planSequence id={} no surviving exit — keep plan delay={}",
                                     single.planId, single.delay));
        return single;
    }
    if (std::find(allowed.begin(), allowed.end(), best.d0) == allowed.end())
        return single;

    if (best.d0 == 0) {
        single.action = PlanAction::Pass;
        single.reason = AssistReason::PassD0Safe;
        single.delay = 0;
        single.nowIsSafe = true;
    } else {
        single.action = PlanAction::Delay;
        single.delay = best.d0;
        single.nowIsSafe = false;
        if (!AssistVerdict::isDelay(single.reason)) {
            single.reason =
                AssistVerdict::inferDelayReason(single.activatedRing, single.requireOrb, false);
        }
    }
    AssistLog::debug(fmt::format(
        "planSequence id={} depth={} d0={} verdict={} allowed={} combos={} bakeChain={}",
        single.planId, depth, best.d0, AssistVerdict::name(single.reason), allowed.size(), combos,
        useBakeChain));
    return single;
}

ShadowSimulator::ReplayResult ShadowSimulator::replay(PlayLayer* layer, int slot, int startTick,
                                                      std::vector<InputEvent> const& timeline,
                                                      float deltaTime, int maxTicks,
                                                      bool shiftActionBy, int actionShift) {
    ReplayResult out;
    auto* snap = snapshotSlot(slot);
    if (!layer || !snap || !m_shadow || deltaTime <= 0.f || maxTicks <= 0)
        return out;

    copyPlayerState(m_shadow, snap);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* shadowPlayer = m_shadow;
    // Build a shifted timeline view: each event tick += actionShift if requested.
    auto effectiveTick = [&](InputEvent const& ev) {
        if (!shiftActionBy)
            return ev.tick;
        return ev.tick + actionShift;
    };

    size_t nextEv = 0;
    // Apply any events at or before startTick immediately.
    while (nextEv < timeline.size() && effectiveTick(timeline[nextEv]) <= startTick) {
        if (timeline[nextEv].down)
            shadowPlayer->pushButton(PlayerButton::Jump);
        else
            shadowPlayer->releaseButton(PlayerButton::Jump);
        ++nextEv;
    }

    this->m_simTickBase = std::max(0, startTick);
    this->m_simAbsTick = -1;
    {
        SimSessionGuard session(*this, layer);

        for (int tick = 0; tick < maxTicks; ++tick) {
            int const absTick = startTick + tick;
            this->m_simAbsTick = absTick;

            while (nextEv < timeline.size() && effectiveTick(timeline[nextEv]) == absTick) {
                if (timeline[nextEv].down)
                    shadowPlayer->pushButton(PlayerButton::Jump);
                else
                    shadowPlayer->releaseButton(PlayerButton::Jump);
                ++nextEv;
            }

            stepPhysics(layer, shadowPlayer, deltaTime, deltaTime);
            out.endX = shadowPlayer->getPositionX();
            if (m_dead) {
                out.died = true;
                out.deathTick = tick;
                out.deathObject = m_deathObject;
                break;
            }
            if (m_unknown) {
                out.unknown = true;
                break;
            }
        }

        out.activatedRing = m_activatedRing;
        out.ringCount = m_ringCount;
    }
    return out;
}

ShadowSimulator::BakeRunResult
ShadowSimulator::runBakeTimeline(PlayLayer* layer, int slot, std::vector<ReplayEvent> const& events,
                                 float deltaTime, int untilFrame, int sampleFrame,
                                 bool applyP2ToShadow, bool traceFrames, bool parkReals,
                                 int substeps, std::function<bool(int frame)> onFrame) {
    BakeRunResult out;
    auto* snap = this->snapshotSlot(slot);
    if (!layer || !snap || !m_shadow || deltaTime <= 0.f || untilFrame < 0)
        return out;
    if (substeps < 1)
        substeps = 1;
    if (substeps > 32)
        substeps = 32;
    float const subDt = deltaTime / static_cast<float>(substeps);

    // Live players share spawn with the ghost — any residual collision path
    // (not only collisionCheckObjects) can mark frame-0 death. Park them.
    cocos2d::CCPoint savedP1{};
    cocos2d::CCPoint savedP2{};
    bool parkedP1 = false;
    bool parkedP2 = false;
    if (parkReals) {
        if (layer->m_player1 && layer->m_player1 != m_shadow) {
            savedP1 = layer->m_player1->getPosition();
            layer->m_player1->setPosition({-100000.f, -100000.f});
            parkedP1 = true;
        }
        if (layer->m_player2 && layer->m_player2 != m_shadow) {
            savedP2 = layer->m_player2->getPosition();
            layer->m_player2->setPosition({-100000.f, -100000.f});
            parkedP2 = true;
        }
    }

    copyPlayerState(m_shadow, snap);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_toggleUnknownHit = false;
    m_deathObject = nullptr;
    this->m_unknownHistMap.clear();
    this->m_collectUnknownHist = true;

    auto* shadowPlayer = m_shadow;
    size_t nextEv = 0;
    if (traceFrames) {
        out.frames.reserve(static_cast<size_t>(std::min(untilFrame, 2048)) + 2);
    }

    auto applyEv = [&](ReplayEvent const& ev) {
        if (ev.player2 && !applyP2ToShadow)
            return;
        auto buttonValue = PlayerButton::Jump;
        if (ev.button == 2)
            buttonValue = PlayerButton::Left;
        else if (ev.button == 3)
            buttonValue = PlayerButton::Right;
        if (ev.down)
            shadowPlayer->pushButton(buttonValue);
        else
            shadowPlayer->releaseButton(buttonValue);
    };

    this->m_simTickBase = 0;
    this->m_simAbsTick = -1;
    this->m_poseRemapDt = deltaTime; // TW remap must match bake physics, not live hitch dt
    this->m_bakeOffline = true;
    {
        SimSessionGuard session(*this, layer);

        for (int frame = 0; frame <= untilFrame; ++frame) {
            this->m_simAbsTick = frame;

            // Clickbots often put press+release on the SAME frame. Apply downs
            // BEFORE physics so the tick is held; releases AFTER the step.
            std::vector<ReplayEvent const*> downs;
            std::vector<ReplayEvent const*> ups;
            while (nextEv < events.size() && events[nextEv].frame == frame) {
                auto const& ev = events[nextEv];
                if (!ev.player2 || applyP2ToShadow) {
                    if (ev.down)
                        downs.push_back(&ev);
                    else
                        ups.push_back(&ev);
                }
                ++nextEv;
            }
            for (auto const* event : downs)
                applyEv(*event);
            bool const jumpHeldThisStep =
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];

            if (sampleFrame >= 0 && frame == sampleFrame) {
                out.xAtSample = shadowPlayer->getPositionX();
                out.modeAtSample = StageConfig::kindOf(shadowPlayer);
                out.dashingAtSample = shadowPlayer->m_isDashing;
                out.sampled = true;
            }

            // Physics-bypass: N micro-steps at author dt inside one bake tick.
            for (int sub = 0; sub < substeps; ++sub) {
                stepPhysics(layer, shadowPlayer, subDt, subDt);
                if (m_dead)
                    break;
            }
            for (auto const* event : ups)
                applyEv(*event);

            // Hard death only — Unknown must not wipe a working replay bake
            // (toggle-group solids used to abort every click via m_unknown).
            if (m_unknown) {
                out.unknown = true;
            }

            if (onFrame && !onFrame(frame)) {
                out.died = m_dead;
                out.deathFrame = frame;
                out.deathX = shadowPlayer->getPositionX();
                out.deathY = shadowPlayer->getPositionY();
                break;
            }

            int const evThis = static_cast<int>(downs.size() + ups.size());
            if (traceFrames) {
                BakeFrameTrace tr;
                tr.frame = frame;
                tr.x = shadowPlayer->getPositionX();
                tr.y = shadowPlayer->getPositionY();
                tr.yVel = static_cast<float>(shadowPlayer->m_yVelocity);
                tr.jumpHeld = jumpHeldThisStep;
                tr.dead = m_dead;
                tr.unknown = m_unknown;
                tr.twApplied = this->m_triggerWorldAppliedThisTick;
                tr.eventsApplied = evThis;
                tr.mode = static_cast<int>(StageConfig::kindOf(shadowPlayer));
                if (m_dead && m_deathObject) {
                    tr.deathObjId = m_deathObject->m_uniqueID;
                    tr.deathObjType = static_cast<int>(m_deathObject->m_objectType);
                    tr.deathObjX = m_deathObject->getPositionX();
                    tr.deathObjY = m_deathObject->getPositionY();
                }
                out.frames.push_back(tr);
            }

            if (m_dead) {
                out.died = true;
                out.deathFrame = frame;
                out.deathX = shadowPlayer->getPositionX();
                out.deathY = shadowPlayer->getPositionY();
                if (m_deathObject) {
                    out.deathObjId = m_deathObject->m_uniqueID;
                    out.deathObjType = static_cast<int>(m_deathObject->m_objectType);
                    out.deathObjGameId = m_deathObject->m_objectID;
                }
                break;
            }
        }
    }

    this->m_collectUnknownHist = false;
    this->m_bakeOffline = false;
    // Seal Soft/Hard unknown histogram (count desc).
    out.unknownHist.clear();
    out.unknownHist.reserve(this->m_unknownHistMap.size());
    out.unknownSoftHits = 0;
    out.unknownHardHits = 0;
    for (auto const& kv : this->m_unknownHistMap) {
        out.unknownHist.push_back(kv.second);
        if (kv.second.hard)
            out.unknownHardHits += kv.second.count;
        else
            out.unknownSoftHits += kv.second.count;
    }
    std::sort(out.unknownHist.begin(), out.unknownHist.end(),
              [](UnknownHistBin const& a, UnknownHistBin const& b) {
                  if (a.count != b.count)
                      return a.count > b.count;
                  return a.objectId < b.objectId;
              });
    this->m_unknownHistMap.clear();

    if (parkedP1 && layer->m_player1) {
        layer->m_player1->setPosition(savedP1);
    }
    if (parkedP2 && layer->m_player2) {
        layer->m_player2->setPosition(savedP2);
    }
    return out;
}

ShadowSimulator::PathSample ShadowSimulator::samplePath(PlayLayer* layer, int slot,
                                                        std::vector<ReplayEvent> const& events,
                                                        float deltaTime, int untilFrame) {
    PathSample out;
    auto* snap = this->snapshotSlot(slot);
    if (!layer || !snap || !m_shadow || deltaTime <= 0.f || untilFrame < 0)
        return out;

    copyPlayerState(m_shadow, snap);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_deathObject = nullptr;

    auto* shadowPlayer = m_shadow;
    size_t nextEv = 0;
    out.points.reserve(static_cast<size_t>(untilFrame) + 2);

    auto applyEv = [&](ReplayEvent const& ev) {
        auto buttonValue = PlayerButton::Jump;
        if (ev.button == 2)
            buttonValue = PlayerButton::Left;
        else if (ev.button == 3)
            buttonValue = PlayerButton::Right;
        if (ev.down)
            shadowPlayer->pushButton(buttonValue);
        else
            shadowPlayer->releaseButton(buttonValue);
    };

    this->m_simTickBase = 0;
    this->m_simAbsTick = -1;
    this->m_poseRemapDt = deltaTime;
    {
        SimSessionGuard session(*this, layer);

        out.points.push_back(shadowPlayer->getPosition());
        for (int frame = 0; frame <= untilFrame; ++frame) {
            this->m_simAbsTick = frame;
            while (nextEv < events.size() && events[nextEv].frame == frame) {
                applyEv(events[nextEv]);
                ++nextEv;
            }
            stepPhysics(layer, shadowPlayer, deltaTime, deltaTime);
            out.points.push_back(shadowPlayer->getPosition());
            if (m_dead || m_unknown) {
                out.died = true;
                out.deathFrame = frame;
                break;
            }
        }
    }
    return out;
}

bool ShadowSimulator::regressionSelfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto binOf = [](int ticks) {
        if (ticks < 24)
            return 0;
        if (ticks < 48)
            return 1;
        if (ticks < 96)
            return 2;
        return 3;
    };
    auto clears = [](RunResult const& r, ThreatZone const& zone) {
        if (!zone.valid)
            return !r.died || r.unknownTrunc;
        if (!r.died)
            return r.endX > zone.endX || r.zoneCleared;
        return r.deathX > zone.endX;
    };

    // Attempt-start bake slot must stay out of the rotating ring.
    expect(START_SNAPSHOT_SLOT == 0, "start-slot-0");
    expect(SNAPSHOT_RING_BEGIN == 1, "ring-begins-1");
    expect(SNAPSHOT_RING_BEGIN < SNAPSHOT_RING_END, "ring-lt-end");
    expect(SNAPSHOT_RING_END == PARITY_SLOT_A, "parity-after-ring");
    expect(PARITY_SLOT_B == SNAPSHOT_SLOTS - 1, "parity-b-last");
    expect(SNAPSHOT_RING_END < SNAPSHOT_SLOTS, "ring-end-lt-slots");

    // Fixed survive bins [0,24), [24,48), [48,96), [96,∞)
    expect(binOf(0) == 0, "bin0");
    expect(binOf(23) == 0, "bin23");
    expect(binOf(24) == 1, "bin24");
    expect(binOf(47) == 1, "bin47");
    expect(binOf(48) == 2, "bin48");
    expect(binOf(95) == 2, "bin95");
    expect(binOf(96) == 3, "bin96");

    ThreatZone zone;
    zone.valid = true;
    zone.startX = 100.f;
    zone.endX = 200.f;

    RunResult inZone;
    inZone.died = true;
    inZone.deathX = 180.f;
    expect(!clears(inZone, zone), "spike-row-not-clear");

    RunResult pastZone;
    pastZone.died = true;
    pastZone.deathX = 201.f;
    expect(clears(pastZone, zone), "past-zone-clear");

    RunResult survivePast;
    survivePast.died = false;
    survivePast.endX = 250.f;
    expect(clears(survivePast, zone), "survive-past-clear");

    RunResult surviveInside;
    surviveInside.died = false;
    surviveInside.endX = 150.f;
    expect(!clears(surviveInside, zone), "survive-inside-not-clear");

    RunResult unk;
    unk.unknownTrunc = true;
    unk.died = false;
    unk.endX = 250.f;
    expect(clears(unk, zone), "unknown-after-clear");

    expect(kClusterGapX == 50.f, "clusterGapX");
    expect(kWindowMarginK == 1, "windowMarginK");
    expect(kDefaultTickBudget == 4000, "tickBudget");
    expect(kFutileEndXGain == 8.f, "futileEndXGain");

    // Wave proximity / futile-delay gates (+ foresight orb/pad must not Pass)
    expect(shouldWaveHazardProximityPass(true, true, true, false, true), "wave-prox-pass");
    expect(!shouldWaveHazardProximityPass(true, true, true, false, false),
           "wave-prox-release-never");
    expect(!shouldWaveHazardProximityPass(true, true, false, false, true),
           "wave-prox-orb-target-blocks");
    expect(!shouldWaveHazardProximityPass(true, true, true, true, true),
           "wave-prox-foresight-blocks");
    expect(!shouldWaveHazardProximityPass(false, true, true, false, true), "wave-prox-not-cube");
    expect(!shouldWaveHazardProximityPass(true, false, true, false, true), "wave-prox-no-hazard");
    expect(shouldWaveFutileDelayPass(true, false, 0, 3.f, false, true), "wave-futile-pass");
    expect(!shouldWaveFutileDelayPass(true, false, 0, 3.f, false, false),
           "wave-futile-release-never");
    expect(!shouldWaveFutileDelayPass(true, true, 0, 3.f, false, true), "wave-futile-orb-intent");
    expect(!shouldWaveFutileDelayPass(true, false, 0, 3.f, true, true), "wave-futile-foresight");
    expect(!shouldWaveFutileDelayPass(true, false, 1, 3.f, false, true), "wave-futile-bin");
    expect(!shouldWaveFutileDelayPass(true, false, 0, 12.f, false, true), "wave-futile-gain");

    // Continuous idleSafer / ascentUseful
    expect(classifyIdleSafer(true, true, false, -1, 0.f, true, 4, 100.f), "idle-safer-alive");
    expect(!classifyIdleSafer(false, true, false, -1, 0.f, true, 4, 100.f), "idle-safer-not-cube");
    expect(!classifyIdleSafer(true, true, false, -1, 0.f, false, -1, 0.f), "idle-safer-d0-safe");
    expect(classifyIdleSafer(true, true, true, 10, 200.f, true, 3, 100.f),
           "idle-safer-later-death");
    expect(deathLooksLikeAscent(100.f, 140.f, false), "ascent-death-above");
    expect(!deathLooksLikeAscent(100.f, 105.f, false), "ascent-death-near");
    expect(deathLooksLikeAscent(100.f, 60.f, true), "ascent-death-flipped");
    expect(classifyAscentUseful(true, true, false, 0, 50.f, true, 0, 50.f, true),
           "ascent-later-viable");
    expect(classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f, true),
           "ascent-better-bin");
    expect(!classifyAscentUseful(true, true, true, 1, 50.f, true, 1, 52.f, true), "ascent-no-gain");
    // Dead soft / high bin without Alive later must not invent ascentUseful.
    expect(!classifyAscentUseful(true, true, false, 0, 50.f, false, 3, 90.f, false),
           "ascent-dead-not-useful");
    expect(!classifyAscentUseful(true, true, false, 0, 50.f, true, 3, 90.f, false),
           "ascent-soft-died-not-useful");
    expect(!classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f, false),
           "ascent-alive-flag-required");

    expect(pickPreferredRelease(2, 10, true) == 2, "pref-rel-cont");
    expect(pickPreferredRelease(2, 10, false) == 6, "pref-rel-tap");
    expect(pickPreferredRelease(-1, 5, false) < 0, "pref-rel-unset");
    expect(pickPreferredRelease(4, 4, false) == 4, "pref-rel-same");

    expect(autoRobustKHints(true, true, 1.f, 0.f, false) == 2, "k-wave");
    expect(autoRobustKHints(true, true, 1.2f, 0.f, true) == 3, "k-wave-spam-spd");
    expect(autoRobustKHints(true, true, 1.f, 0.f, true) == 2, "k-wave-spam-slow");
    expect(autoRobustKHints(false, false, 1.f, 0.f, false) == 1, "k-cube");
    expect(autoRobustKHints(false, false, 1.6f, 0.f, false) == 3, "k-fast");
    expect(autoRobustKHints(true, false, 1.f, 0.1f, false) == 3, "k-dt-bump");

    // Synthetic early-click snaps: d=0 dies in zone → Delay must clear past zoneEndX.
    class Snap {
    public:
        float deathX;
        int clearDelay;

    private:
    };
    Snap snaps[] = {
        {150.f, 4}, {120.f, 3}, {190.f, 6}, {105.f, 2}, {175.f, 5},
    };
    for (auto const& s : snaps) {
        expect(s.clearDelay > 0 && s.deathX <= zone.endX, "snap-needs-delay");
        RunResult d0;
        d0.died = true;
        d0.deathX = s.deathX;
        expect(!clears(d0, zone), "snap-d0-unsafe");
        RunResult dn;
        dn.died = false;
        dn.endX = zone.endX + 40.f;
        expect(clears(dn, zone), "snap-delay-clears");
    }

    // Score honesty: soft-viable surviveBin Escape is gone.
    expect(!ShadowSimulatorImplementation::delayViableHonest(false, false, false, false, false,
                                                             false, true),
           "soft-viable-no-clear");
    expect(ShadowSimulatorImplementation::delayViableHonest(false, false, false, false, true, false,
                                                            true),
           "viable-clears-threat");
    expect(ShadowSimulatorImplementation::delayViableHonest(false, false, false, false, false, true,
                                                            true),
           "viable-zone-cleared");
    expect(ShadowSimulatorImplementation::delayViableHonest(false, false, false, false, false,
                                                            false, false),
           "viable-no-zone");
    // Soft bake: raw death + Landing/LateCrash can be not-false-lethal; Lethal cannot.
    expect(!ShadowSimulatorImplementation::delayViableHonest(false, true, false, false, true, false,
                                                             true, ModeSurvival::Class::Lethal),
           "lethal-died-not-viable");
    expect(ShadowSimulatorImplementation::delayViableHonest(false, true, false, false, true, true,
                                                            true, ModeSurvival::Class::Landing),
           "landing-died-soft-viable");
    expect(
        ShadowSimulatorImplementation::delayViableHonest(
            false, true, false, false, true, true, true, ModeSurvival::Class::LateCrashAfterClear),
        "late-crash-soft-viable");
    // Live Delay-worthy: Alive only; Landing/LateCrash never invent Delay.
    expect(ShadowSimulatorImplementation::delayViableHonest(false, false, false, false, true, false,
                                                            true, ModeSurvival::Class::Alive, true),
           "alive-require-worthy");
    expect(!ShadowSimulatorImplementation::delayViableHonest(
               false, true, false, false, true, true, true, ModeSurvival::Class::Landing, true),
           "landing-not-delay-worthy");
    expect(!ShadowSimulatorImplementation::delayViableHonest(
               false, true, false, false, true, true, true,
               ModeSurvival::Class::LateCrashAfterClear, true),
           "late-crash-not-delay-worthy");
    expect(!ShadowSimulatorImplementation::delayViableHonest(
               false, true, false, false, true, true, true, ModeSurvival::Class::Recoverable, true),
           "recoverable-not-delay-worthy");
    expect(!ShadowSimulatorImplementation::delayViableHonest(false, false, true, false, true, false,
                                                             true),
           "unknown-not-viable");
    expect(!ShadowSimulatorImplementation::delayViableHonest(false, false, false, true, true, false,
                                                             true),
           "toggle-unk-not-viable");
    expect(ShadowSimulatorImplementation::exitSurvivedHonest(false, false), "exit-alive");
    expect(!ShadowSimulatorImplementation::exitSurvivedHonest(true, false), "exit-died");
    expect(!ShadowSimulatorImplementation::exitSurvivedHonest(false, true),
           "exit-unknown-not-survive");
    expect(!ShadowSimulatorImplementation::exitSurvivedHonest(true, true), "exit-died-unk");

    if (fails == 0)
        report = "regressionSelfCheck OK (bins+zone+5 snaps+score-honesty)";
    else
        report = fmt::format("regressionSelfCheck {} fails: {}", fails, report);
    return fails == 0;
}

void ShadowSimulator::applyPlayerBlob(PlayerObject* destination, DevPlayerBlob const& blob) {
    if (!destination || !blob.present)
        return;
    destination->setPosition({blob.x, blob.y});
    destination->m_position = cocos2d::CCPoint{blob.x, blob.y};
    destination->setRotation(blob.rotation);
    destination->setScaleX(blob.scaleX);
    destination->setScaleY(blob.scaleY);
    destination->m_yVelocity = blob.yVelocity;
    destination->m_fallSpeed = blob.fallSpeed;
    destination->m_gravity = blob.gravity;
    destination->m_gravityMod = blob.gravityMod;
    destination->m_speedMultiplier = blob.speedMultiplier;
    destination->m_playerSpeed = blob.playerSpeed;
    destination->m_vehicleSize = blob.vehicleSize;
    destination->m_platformerXVelocity = blob.platformerXVelocity;
    destination->m_isUpsideDown = blob.isUpsideDown;
    destination->m_isSideways = blob.isSideways;
    destination->m_isGoingLeft = blob.isGoingLeft;
    destination->m_isShip = blob.isShip;
    destination->m_isBall = blob.isBall;
    destination->m_isBird = blob.isBird;
    destination->m_isSwing = blob.isSwing;
    destination->m_isDart = blob.isDart;
    destination->m_isRobot = blob.isRobot;
    destination->m_isSpider = blob.isSpider;
    destination->m_isSecondPlayer = blob.isSecondPlayer;
    destination->m_isPlatformer = blob.isPlatformer;
    destination->m_isDashing = blob.isDashing;
    destination->m_isOnGround = blob.isOnGround;
    destination->m_isOnGround2 = blob.isOnGround2;
    destination->m_isOnSlope = blob.isOnSlope;
    destination->m_wasOnSlope = blob.wasOnSlope;
    destination->m_jumpBuffered = blob.jumpBuffered;
    destination->m_wasJumpBuffered = blob.wasJumpBuffered;
    destination->m_stateRingJump = blob.stateRingJump;
    destination->m_hasEverJumped = blob.hasEverJumped;
    destination->m_hasEverHitRing = blob.hasEverHitRing;
    destination->m_isLocked = blob.isLocked;
    destination->m_controlsDisabled = blob.controlsDisabled;
    destination->m_maybeIsFalling = blob.maybeIsFalling;
    destination->m_maybeIsBoosted = blob.maybeIsBoosted;
    destination->m_isDead = false;
    hideGhost(destination);
}

void ShadowSimulator::applyEvalWorldRestore(PlayLayer* layer, DevLiveWitness const& witness,
                                            DevEvalRestore& restore) {
    if (!layer)
        return;

    // Snapshot live activated-ID map + clear per-run object flag saves.
    this->m_saved.clear();
    this->m_savedTransforms.clear();
    this->m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;

    // --- Dual ghost ---
    if (witness.dual.present && this->m_evalGhosts[1]) {
        applyPlayerBlob(this->m_evalGhosts[1], witness.dual);
        DevEvalRestore::noteDualWritten(restore, witness.dual);
    }

    // --- Orbs: set activated ID set + mark matching objects claimed ---
    {
        bool const wantOrbs = DevLiveWitness::hasWorldDep(witness, DevWitnessCategory::Orbs);
        if (wantOrbs || !witness.orbIds.empty()) {
            layer->m_gameState.m_activatedObjectIDs.clear();
            auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
            for (int uid : witness.orbIds) {
                if (uid == 0)
                    continue;
                std::pair<int, int> const activatedKey{uid, 0};
                layer->m_gameState.m_activatedObjectIDs[activatedKey] = 1;
                if (!children)
                    continue;
                for (unsigned index = 0; index < children->count(); ++index) {
                    auto* object = geode::prelude::typeinfo_cast<EnhancedGameObject*>(
                        children->objectAtIndex(index));
                    if (!object || object->m_uniqueID != uid)
                        continue;
                    this->noteActivated(object);
                    object->m_activated = true;
                    object->m_isActivated = true;
                    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object)) {
                        ring->m_claimTouch = true;
                    }
                    break;
                }
            }
            DevEvalRestore::noteOrbsWritten(restore, witness.orbIds);
        }
    }

    // --- Triggers: spool store poses at witness apply tick ---
    if (witness.triggerCanSpool) {
        this->captureTriggerTransforms(layer);
        int const tick =
            witness.triggerApplyTick >= 0 ? witness.triggerApplyTick : witness.clock.effectTick;
        TriggerWorldStore::get().applyAtTick(layer, tick);
        DevEvalRestore::noteTriggersSpooled(restore, witness.triggerStoreKey,
                                            witness.triggerStoreVersion, tick);
    }

    // --- Movers: freeze-world poses, else digest-only ---
    if (witness.freezeWorld) {
        auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
        if (children) {
            for (auto const& pose : witness.moverPoses) {
                GameObject* object = nullptr;
                for (unsigned index = 0; index < children->count(); ++index) {
                    auto* candidate =
                        geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
                    if (!candidate)
                        continue;
                    if (candidate->m_uniqueID != pose.uniqueId)
                        continue;
                    object = candidate;
                    break;
                }
                if (!object)
                    continue;

                bool already = false;
                for (auto const& st : this->m_savedTransforms) {
                    if (st.uniqueId == pose.uniqueId) {
                        already = true;
                        break;
                    }
                }
                if (!already) {
                    SavedTransform st;
                    st.uniqueId = object->m_uniqueID;
                    st.obj = object;
                    st.x = object->getPositionX();
                    st.y = object->getPositionY();
                    st.rotation = object->getRotation();
                    st.scaleX = object->getScaleX();
                    st.scaleY = object->getScaleY();
                    st.nodeVisible = object->isVisible();
                    this->m_savedTransforms.push_back(st);
                }

                TriggerPose tp;
                tp.tick = 0;
                tp.x = pose.x;
                tp.y = pose.y;
                tp.rotation = pose.rotation;
                tp.scaleX = pose.scaleX;
                tp.scaleY = pose.scaleY;
                tp.active = pose.active;
                TriggerWorldStore::writePose(object, tp);
            }
        }
        DevEvalRestore::noteMoversPosesWritten(restore, witness.moverPoses,
                                               witness.hasHazardDigest ? witness.hazardDigest : 0);
    } else if (witness.hasHazardDigest) {
        DevEvalRestore::noteMoversDigestOnly(restore, witness.hazardDigest);
    }
}

void ShadowSimulator::finishEvalWorldRestore(PlayLayer* layer) {
    if (!layer) {
        this->m_saved.clear();
        this->m_savedTransforms.clear();
        this->m_savedActivatedIDs.clear();
        return;
    }
    {
        LagProbe::Scope const scope("eb.trigger.restore");
        this->restoreTriggerTransforms(layer);
    }
    for (auto const& s : this->m_saved) {
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
            static_cast<RingObject*>(s.obj)->m_claimTouch = s.claimTouch;
        }
    }
    this->m_saved.clear();
    layer->m_gameState.m_activatedObjectIDs = this->m_savedActivatedIDs;
    this->m_savedActivatedIDs.clear();
}

ShadowSimulator::EvalFromFreezeResult
ShadowSimulator::evalDelayFromFreeze(PlayLayer* layer, DevLiveWitness const& witness, int delay,
                                     int horizonTicks, bool down, int* tickBudgetLeft) {
    EvalFromFreezeResult out;
    out.evalSimPathId = DevSimPathId::EvalDelayFromFreeze;
    this->m_evalAbort = false;

    if (!witness.valid || !witness.clock.valid || !DevClockContract::get().clockOk(witness.clock)) {
        out.clockMismatch = true;
        return out;
    }
    if (!layer || !this->m_evalGhosts[0] || layer != this->m_hostLayer) {
        return out;
    }
    if (!witness.player.present) {
        return out;
    }
    if (delay < 0)
        delay = 0;
    if (horizonTicks < 0)
        horizonTicks = 0;

    auto& clock = DevClockContract::get();
    auto steps = clock.stepSchedule(witness.clock, delay, horizonTicks);
    out.stepScheduleHash = clock.hashSteps(steps);
    if (steps.empty())
        return out;

    out.restore = DevEvalRestore::beginFromWitness(witness, DevSimPathId::EvalDelayFromFreeze);

    PlayerObject* ghost = this->m_evalGhosts[0];
    applyPlayerBlob(ghost, witness.player);
    DevEvalRestore::notePlayerWritten(out.restore, witness.player);
    this->applyEvalWorldRestore(layer, witness, out.restore);

    this->m_evalActive = ghost;
    this->m_simulating = true;
    this->m_dead = false;
    this->m_jumped = false;
    this->m_unknown = false;
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_deathObject = nullptr;
    this->m_simTickBase = witness.clock.effectTick;

    ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool const tapRelease =
        !ghost->m_isDart && !ghost->m_isShip && !ghost->m_isSwing && !ghost->m_isRobot;
    bool acted = false;
    bool tapReleased = false;
    out.actionTick = delay;
    out.ran = true;

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft)
            return false;
        if (*tickBudgetLeft <= 0)
            return true;
        --(*tickBudgetLeft);
        return false;
    };

    if (delay > 0) {
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase;
        this->m_triggerWorldAppliedThisTick = false;
        stepPreCollide(layer, ghost, witness.clock.simDt);
        if (this->m_dead) {
            out.ticksRan = 1;
            out.died = true;
            out.deathTick = 0;
            out.endX = ghost->getPositionX();
        } else if (this->m_unknown) {
            out.ticksRan = 1;
            out.unknown = true;
            out.endX = ghost->getPositionX();
        }
    }

    int const countValue = static_cast<int>(steps.size());
    for (int tick = 0; tick < countValue && !out.died && !out.unknown; ++tick) {
        if (this->m_evalAbort) {
            out.aborted = true;
            break;
        }
        if (spend()) {
            out.budgetHit = true;
            break;
        }
        float const stepDt = steps[static_cast<size_t>(tick)];
        out.ticksRan = tick + 1;
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase + tick;
        this->m_triggerWorldAppliedThisTick = false;

        if (!acted && tick == delay) {
            acted = true;
            if (down) {
                ghost->pushButton(PlayerButton::Jump);
                ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else {
                ghost->releaseButton(PlayerButton::Jump);
                ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && down && tapRelease && !tapReleased && this->m_jumped) {
            bool const touchingOrb =
                this->m_orbOverlapTick ||
                (ghost->m_touchingRings && ghost->m_touchingRings->count() > 0);
            bool const dashing = ghost->m_isDashing;
            bool const justHitOrb = this->m_activatedRing && tick <= delay + 4;
            if (!touchingOrb && !dashing && !justHitOrb) {
                tapReleased = true;
                ghost->releaseButton(PlayerButton::Jump);
            }
        }

        stepPhysics(layer, ghost, stepDt, witness.clock.simDt);
        if (this->m_dead) {
            out.died = true;
            out.deathTick = tick;
            out.endX = ghost->getPositionX();
            break;
        }
        if (this->m_unknown) {
            out.unknown = true;
            out.endX = ghost->getPositionX();
            break;
        }
        out.endX = ghost->getPositionX();
    }

    this->m_simAbsTick = -1;
    this->m_simulating = false;
    this->m_evalActive = nullptr;
    this->finishEvalWorldRestore(layer);
    // Park eval ghost (same evacuate rules as shadow).
    if (auto* pl = PlayLayer::get()) {
        if (pl == this->m_hostLayer) {
            tryRemoveFromSection(pl, ghost);
        }
    }
    ghost->m_outerSectionIndex = -1;
    ghost->m_innerSectionIndex = -1;
    ghost->m_middleSectionIndex = -1;
    ghost->setPosition({0.f, 105.f});
    ghost->m_position = cocos2d::CCPoint{0.f, 105.f};
    hideGhost(ghost);
    if (this->m_evalGhosts[1]) {
        hideGhost(this->m_evalGhosts[1]);
        this->m_evalGhosts[1]->setPosition({0.f, 105.f});
        this->m_evalGhosts[1]->m_position = cocos2d::CCPoint{0.f, 105.f};
    }
    return out;
}

ShadowSimulator::EvalFromFreezeResult
ShadowSimulator::replayActualFire(PlayLayer* layer, DevLiveWitness const& witness,
                                  int actualFireTickAbs, int horizonTicks, bool down,
                                  int* tickBudgetLeft) {
    EvalFromFreezeResult out;
    out.evalSimPathId = DevSimPathId::ReplayActualFire;
    this->m_evalAbort = false;

    if (!witness.valid || !witness.clock.valid || !DevClockContract::get().clockOk(witness.clock)) {
        out.clockMismatch = true;
        return out;
    }
    // Delay from effectTick to actual fire (clamped ≥ 0).
    int delay = actualFireTickAbs - witness.clock.effectTick;
    if (delay < 0)
        delay = 0;

    // Reuse the schedule path but retag evalSimPathId / restore path id.
    out = this->evalDelayFromFreeze(layer, witness, delay, horizonTicks, down, tickBudgetLeft);
    out.evalSimPathId = DevSimPathId::ReplayActualFire;
    if (out.restore.valid) {
        out.restore.evalSimPathId = DevSimPathId::ReplayActualFire;
    }
    return out;
}
