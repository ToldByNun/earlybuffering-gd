#include "ShadowSimulator.hpp"
#include "AssistClock.hpp"
#include "BakeMap.hpp"
#include "MissAssistContracts.hpp"
#include "TravelForesight.hpp"
#include "OpportunityProbe.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"
#include "../debug/LagProbe.hpp"
#include "../debug/PerfTrace.hpp"
#include "TriggerWorldStore.hpp"
#include "../dev/DevConfig.hpp"
#include "../dev/DevRejectLedger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace geode::prelude;


constexpr int BRANCH_RELEASES[] = { 2, 5, 10, 18 };
// Post-pick hold probe: how long can the player hold, and when to let go safely.
constexpr int HOLD_PROBE_RELEASES[] = { 0, 1, 2, 3, 5, 8, 10, 14, 18, 24, 32, 40 };
constexpr int HOLD_PROBE_MAX = 8; // budget: at most this many extra runOnes


ShadowSimulator& ShadowSimulator::get() {
    static ShadowSimulator inst;
    return inst;
}

void ShadowSimulator::attach(PlayLayer* layer) {
    detach();
    if (!layer || !layer->m_objectLayer) return;

    auto makeHidden = [&](char const* id) -> PlayerObject* {
        auto* p = PlayerObject::create(1, 1, layer, layer, true);
        if (!p) return nullptr;
        p->setPosition({ 0.f, 105.f });
        p->setID(id);
        // objectLayer owns the node (addChild retain). Do NOT extra-retain —
        // release-on-quit while still parented / double-free in ~PlayerObject
        // during setNextScene was the quit AV (CCObject::release).
        layer->m_objectLayer->addChild(p);
        hideGhost(p);
        return p;
    };

    m_shadow = makeHidden("earlybuffer-shadow-player"_spr);
    for (int i = 0; i < SNAPSHOT_SLOTS; ++i) {
        auto const id = fmt::format("earlybuffer-snapshot-{}", i);
        m_snapshots[i] = makeHidden(id.c_str());
    }
    for (int i = 0; i < DEV_EVAL_SLOTS; ++i) {
        auto const id = fmt::format("earlybuffer-eval-{}", i);
        m_evalGhosts[i] = makeHidden(id.c_str());
    }
    m_evalActive = nullptr;
    m_evalAbort = false;
    m_nextSnapshot = 0;
    m_hostLayer = layer;
}

void ShadowSimulator::detach(bool layerDying) {
    // Never leave the sim flag set — quit can interrupt a traj rebuild mid-capture.
    this->m_simulating = false;
    this->m_applyingRing = false;
    this->m_orbScanning = false;
    this->m_orbOverlapTick = false;
    this->m_dead = false;
    this->m_jumped = false;
    this->m_unknown = false;
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_deathObject = nullptr;
    this->m_simAbsTick = -1;
    this->m_simTickBase = 0;
    this->m_triggerWorldAppliedThisTick = false;
    this->m_dynamicTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    this->m_saved.clear();
    this->m_savedTransforms.clear();
    // gd::map: `= {}` is ambiguous on libstdc++/Android — use clear().
    this->m_savedActivatedIDs.clear();
    // Clear host first so a nested evacuate (if any) sees "not attached".
    this->m_hostLayer = nullptr;

    auto sanitize = [](PlayerObject* p) {
        if (!p) return;
        p->m_outerSectionIndex = -1;
        p->m_innerSectionIndex = -1;
        p->m_middleSectionIndex = -1;
        if (p->m_touchingRings) {
            p->m_touchingRings->removeAllObjects();
        }
        if (p->m_waveTrail) {
            p->m_waveTrail->reset();
        }
        p->setVisible(false);
    };

    auto kill = [&](PlayerObject*& p) {
        if (!p) return;
        sanitize(p);
        if (layerDying) {
            // Parent tree is already tearing down — do not removeFromParent /
            // release (double-free in ~PlayerObject during setNextScene).
            p = nullptr;
            return;
        }
        // Normal quit / reattach: destroy while PlayLayer is still alive.
        if (p->getParent()) {
            p->removeFromParentAndCleanup(true);
        }
        p = nullptr;
    };
    kill(this->m_shadow);
    for (auto& s : this->m_snapshots) kill(s);
    for (auto& e : this->m_evalGhosts) kill(e);
    this->m_evalActive = nullptr;
    this->m_evalAbort = false;
}

bool ShadowSimulator::isOwnedGhost(PlayerObject* p) const {
    if (!p) return false;
    if (p == m_shadow) return true;
    for (auto* s : m_snapshots) {
        if (p == s) return true;
    }
    for (auto* e : m_evalGhosts) {
        if (p == e) return true;
    }
    return false;
}

void ShadowSimulator::abortEval() {
    this->m_evalAbort = true;
}

bool ShadowSimulator::tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* obj) {
    // Never call GJBaseGameLayer::removeObjectFromSection — it AVs when indices
    // are stale, sections are half-built (init/setupHasCompleted), or mx is bad
    // (crash read @ 0xB8). Erase our pointer from the inner vector ourselves.
    if (!layer || !obj) return false;
    int const ox = obj->m_outerSectionIndex;
    int const ix = obj->m_innerSectionIndex;
    if (ox < 0 || ix < 0) {
        obj->m_outerSectionIndex = -1;
        obj->m_innerSectionIndex = -1;
        obj->m_middleSectionIndex = -1;
        return false;
    }

    auto& sections = layer->m_sections;
    bool erased = false;
    if (ox < static_cast<int>(sections.size())) {
        auto* mid = sections[static_cast<size_t>(ox)];
        if (mid && ix < static_cast<int>(mid->size())) {
            auto* inner = (*mid)[static_cast<size_t>(ix)];
            if (inner) {
                for (int i = static_cast<int>(inner->size()) - 1; i >= 0; --i) {
                    if ((*inner)[static_cast<size_t>(i)] != obj) continue;
                    inner->erase(inner->begin() + static_cast<std::ptrdiff_t>(i));
                    erased = true;
                    break;
                }
            }
        }
    }

    obj->m_outerSectionIndex = -1;
    obj->m_innerSectionIndex = -1;
    obj->m_middleSectionIndex = -1;
    return erased;
}

void ShadowSimulator::scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, ShadowSimulator* self) {
    if (!layer || !self) return;

    // Only match our known ghost pointers by raw address. Section slots can hold
    // freed/poison entries (0xFFF…); typeinfo_cast on those AVs (read @ -1).
    GameObject* ghosts[1 + SNAPSHOT_SLOTS + DEV_EVAL_SLOTS]{};
    int ghostCount = 0;
    if (self->m_shadow) {
        ghosts[ghostCount++] = self->m_shadow;
    }
    for (auto* slot : self->m_snapshots) {
        if (!slot) continue;
        ghosts[ghostCount++] = slot;
    }
    for (auto* eg : self->m_evalGhosts) {
        if (!eg) continue;
        ghosts[ghostCount++] = eg;
    }
    if (ghostCount <= 0) return;

    auto isOwnedSlot = [&](GameObject* obj) -> bool {
        if (!obj) return false;
        auto const addr = reinterpret_cast<uintptr_t>(obj);
        // Reject null-page and classic poison (-1) without dereferencing.
        if (addr < 0x10000ull || addr == ~uintptr_t{0}) return false;
        for (int g = 0; g < ghostCount; ++g) {
            if (ghosts[g] == obj) return true;
        }
        return false;
    };

    auto scrubCell = [&](auto* inner, int& found) {
        if (!inner || found >= ghostCount) return;
        for (int i = static_cast<int>(inner->size()) - 1; i >= 0; --i) {
            if (static_cast<size_t>(i) >= inner->size()) continue;
            auto* obj = (*inner)[static_cast<size_t>(i)];
            if (!isOwnedSlot(obj)) continue;
            inner->erase(inner->begin() + static_cast<std::ptrdiff_t>(i));
            ++found;
            if (found >= ghostCount) return;
        }
    };

    auto& sections = layer->m_sections;
    int found = 0;

    // Fast path: only touch cells named by each ghost's section indices.
    for (int g = 0; g < ghostCount; ++g) {
        auto* obj = ghosts[g];
        if (!obj) continue;
        int const ox = obj->m_outerSectionIndex;
        int const ix = obj->m_innerSectionIndex;
        int const mx = obj->m_middleSectionIndex;
        if (ox < 0 || ix < 0 || mx < 0) continue;
        if (ox >= static_cast<int>(sections.size())) continue;
        auto* mid = sections[static_cast<size_t>(ox)];
        if (!mid || ix >= static_cast<int>(mid->size())) continue;
        scrubCell((*mid)[static_cast<size_t>(ix)], found);
        if (found >= ghostCount) return;
    }

    // Slow path only if some ghosts still might linger (bad / -1 indices).
    // Early-exit once every owned pointer was erased — never walk the whole
    // grid "just in case" after that (was ~20ms/restart on large levels).
    if (found >= ghostCount) return;

    size_t const outerN = sections.size();
    for (size_t ox = 0; ox < outerN; ++ox) {
        auto* mid = sections[ox];
        if (!mid) continue;
        size_t const midN = mid->size();
        for (size_t ix = 0; ix < midN; ++ix) {
            scrubCell((*mid)[ix], found);
            if (found >= ghostCount) return;
        }
    }
}

void ShadowSimulator::parkGhosts() {
    // Only the active sim body — snapshots must keep captured state for
    // bake / outcome verify / trajectory samplePath.
    if (!this->m_shadow) return;

    if (auto* layer = PlayLayer::get()) {
        if (layer == this->m_hostLayer) {
            ShadowSimulator::tryRemoveFromSection(layer, this->m_shadow);
        }
    }
    this->m_shadow->m_outerSectionIndex = -1;
    this->m_shadow->m_innerSectionIndex = -1;
    this->m_shadow->m_middleSectionIndex = -1;

    this->m_shadow->setPosition({ 0.f, 105.f });
    this->m_shadow->m_position = cocos2d::CCPoint{ 0.f, 105.f };
    this->m_shadow->m_yVelocity = 0.0;
    this->m_shadow->m_isDead = false;
    // Wave trail points stay in world space after a sim — clear so leftover
    // streak geometry cannot interact with live collision/debug paths.
    if (this->m_shadow->m_waveTrail) {
        this->m_shadow->m_waveTrail->reset();
    }
    hideGhost(this->m_shadow);
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
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_deathObject = nullptr;
    this->m_simAbsTick = -1;
    this->m_simTickBase = 0;
    this->m_triggerWorldAppliedThisTick = false;
    this->m_dynamicTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    // If a sim aborted after applyAtTick moved solids, put them back before
    // we drop the snapshot — reset/checkpoint must not inherit drifted poses.
    if (!this->m_savedTransforms.empty() && layer) {
        LagProbe::Scope const s("eb.trigger.restore");
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
    auto noteIndexed = [&](PlayerObject* p) {
        if (!p) return;
        if (p->m_outerSectionIndex >= 0
            || p->m_innerSectionIndex >= 0
            || p->m_middleSectionIndex >= 0) {
            anyIndexed = true;
        }
    };
    noteIndexed(this->m_shadow);
    for (auto* slot : this->m_snapshots) {
        noteIndexed(slot);
    }
    for (auto* eg : this->m_evalGhosts) {
        noteIndexed(eg);
    }
    LagProbe::get().mark("evacuate.indexed", anyIndexed ? "yes" : "no");
    if (anyIndexed) {
        LagProbe::Scope const s("eb.evacuate.scrub");
        ShadowSimulator::scrubOwnedGhostsFromSections(layer, this);
    }
    // Belt-and-suspenders: index erase if scrub missed a stale cell.
    {
        LagProbe::Scope const s("eb.evacuate.tryRemove");
        ShadowSimulator::tryRemoveFromSection(layer, this->m_shadow);
        for (auto* slot : this->m_snapshots) {
            ShadowSimulator::tryRemoveFromSection(layer, slot);
        }
        for (auto* eg : this->m_evalGhosts) {
            ShadowSimulator::tryRemoveFromSection(layer, eg);
        }
    }

    auto evac = [&](PlayerObject* p) {
        if (!p) return;
        p->m_outerSectionIndex = -1;
        p->m_innerSectionIndex = -1;
        p->m_middleSectionIndex = -1;
        p->setPosition({ 0.f, 105.f });
        p->m_position = cocos2d::CCPoint{ 0.f, 105.f };
        p->m_yVelocity = 0.0;
        p->m_isDead = false;
        if (p->m_waveTrail) {
            p->m_waveTrail->reset();
        }
        if (p->m_touchingRings) {
            p->m_touchingRings->removeAllObjects();
        }
        if (p->m_collisionLogTop) {
            this->clearCollisionLogs(p);
        }
        this->hideGhost(p);
    };

    {
        LagProbe::Scope const s("eb.evacuate.park");
        evac(this->m_shadow);
        for (auto* slot : this->m_snapshots) {
            evac(slot);
        }
        for (auto* eg : this->m_evalGhosts) {
            evac(eg);
        }
    }
    this->m_evalActive = nullptr;
    // Cancel in-flight eval loop; next evalDelayFromFreeze clears the flag.
    this->m_evalAbort = true;
}

void ShadowSimulator::markDead(PlayerObject* p, GameObject* object) {
    // Active sim body (live shadow or eval ghost) drives m_dead.
    if (p == m_shadow || (m_evalActive && p == m_evalActive)) {
        m_dead = true;
        m_deathObject = object;
    }
}

void ShadowSimulator::noteJump() {
    m_jumped = true;
}

void ShadowSimulator::hideGhost(PlayerObject* p) {
    if (!p) return;
    p->setVisible(false);
    p->setOpacity(0);
    p->m_playEffects = false;
    if (p->m_waveTrail) {
        p->m_waveTrail->setVisible(false);
        p->m_waveTrail->setOpacity(0);
    }
    // Shadow particle emitters must not paint the real level during sim.
    auto mute = [](cocos2d::CCParticleSystemQuad* q) {
        if (!q) return;
        q->setVisible(false);
        q->stopSystem();
        q->setEmissionRate(0.f);
    };
    mute(p->m_playerGroundParticles);
    mute(p->m_trailingParticles);
    mute(p->m_shipClickParticles);
    mute(p->m_vehicleGroundParticles);
    mute(p->m_ufoClickParticles);
    mute(p->m_robotBurstParticles);
    mute(p->m_dashParticles);
    mute(p->m_swingBurstParticles1);
    mute(p->m_swingBurstParticles2);
    mute(p->m_landParticles0);
    mute(p->m_landParticles1);
}

void ShadowSimulator::noteActivated(GameObject* gobj) {
    auto* obj = typeinfo_cast<EnhancedGameObject*>(gobj);
    if (!obj) return;
    for (auto const& s : m_saved) {
        if (s.obj == obj) return;
    }
    SavedObject s;
    s.obj = obj;
    s.activated = obj->m_activated;
    s.p1 = obj->m_activatedByPlayer1;
    s.p2 = obj->m_activatedByPlayer2;
    s.poweredOn = obj->m_poweredOn;
    s.state = obj->m_state;
    s.isActivated = obj->m_isActivated;
    s.egoVisible = obj->m_visible;
    s.opacity = obj->getOpacity();
    s.nodeVisible = obj->isVisible();
    if (auto* ring = typeinfo_cast<RingObject*>(obj)) {
        s.isRing = true;
        s.claimTouch = ring->m_claimTouch;
    }
    m_saved.push_back(s);
}

void ShadowSimulator::noteOrbOverlap() {
    m_orbOverlapTick = true;
}

void ShadowSimulator::applyRingJump(PlayerObject* shadow, RingObject* ring) {
    if (!shadow || !ring || (shadow != m_shadow && shadow != m_evalActive)) return;

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
    if (!multi && (ring->m_claimTouch || ring->m_activated)) return;
    if (!multi && shadow->m_touchedRings.count(ring->m_uniqueID)) return;

    noteActivated(ring);
    m_activatedRing = true;
    ++m_ringCount;

    // Real orb physics. Re-entrancy flag lets the ringJump hook call the original.
    // VFX hooks (spawnCircle, activateObject visuals, particles) stay blocked.
    m_applyingRing = true;
    shadow->ringJump(ring, false);
    m_applyingRing = false;

    // Claim for the rest of this run so collision doesn't re-boost every tick.
    // restoreCapture() puts the real orb back afterward.
    if (!multi) {
        ring->m_claimTouch = true;
        ring->m_activated = true;
        ring->m_isActivated = true;
        if (shadow->m_isSecondPlayer) ring->m_activatedByPlayer2 = true;
        else ring->m_activatedByPlayer1 = true;
        shadow->m_touchedRings.insert(ring->m_uniqueID);
    }
    shadow->m_touchedRing = true;
    shadow->m_hasEverHitRing = true;
}

void ShadowSimulator::captureTriggerTransforms(PlayLayer* layer) {
    this->m_savedTransforms.clear();
    if (!layer || !StageConfig::triggerBake()) return;

    auto& store = TriggerWorldStore::get();
    LagProbe::Scope const scope("eb.trigger.capture");

    // Collect uniqueIds we may move this sim (ready static bake tracks).
    std::unordered_set<int> wanted;
    if (store.loaded() && store.hasTracks()) {
        wanted.reserve(store.trackCount());
        for (auto const& track : store.data().tracks) {
            if (!track.ready || track.keys.empty()) continue;
            if (!TriggerClassify::isStatic(track.kind)) continue;
            wanted.insert(track.uniqueId);
        }
    }
    if (wanted.empty()) return;

    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children) return;

    this->m_savedTransforms.reserve(wanted.size());
    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!obj) continue;
        int const uid = obj->m_uniqueID;
        if (wanted.find(uid) == wanted.end()) continue;

        SavedTransform st;
        st.uniqueId = uid;
        st.obj = obj;
        st.x = obj->getPositionX();
        st.y = obj->getPositionY();
        st.rotation = obj->getRotation();
        st.scaleX = obj->getScaleX();
        st.scaleY = obj->getScaleY();
        st.nodeVisible = obj->isVisible();
        st.dirty = false;
        this->m_savedTransforms.push_back(st);
    }
}

bool ShadowSimulator::isDynamicTriggerTarget(int uniqueId) const {
    return this->m_dynamicTargetIds.find(uniqueId) != this->m_dynamicTargetIds.end();
}

bool ShadowSimulator::liveDynamicApplied(int uniqueId) const {
    return this->m_liveDynamicAppliedIds.find(uniqueId) != this->m_liveDynamicAppliedIds.end();
}

void ShadowSimulator::captureDynamicFollows(PlayLayer* layer) {
    this->m_dynamicTargetIds.clear();
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    if (!layer || !StageConfig::triggerBake()) return;

    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children) return;

    bool const wantJobs = !StageConfig::triggerBakeSkipDynamic();

    // First collect DynamicFollow effects only; skip the second full-roster scan when none exist.
    std::vector<EffectGameObject*> followEffects;
    for (unsigned i = 0; i < children->count(); ++i) {
        if (followEffects.size() >= kMaxDynamicFollowJobs) break;
        auto* effect = typeinfo_cast<EffectGameObject*>(children->objectAtIndex(i));
        if (!effect) continue;
        if (effect->m_targetGroupID <= 0) continue;
        if (TriggerClassify::classify(effect) != TriggerKind::DynamicFollow) continue;
        followEffects.push_back(effect);
    }
    if (followEffects.empty()) return;

    // Index solids once for group membership.
    std::vector<GameObject*> solids;
    solids.reserve(static_cast<size_t>(children->count()));
    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!obj) continue;
        if (typeinfo_cast<PlayerObject*>(obj)) continue;
        solids.push_back(obj);
    }

    std::unordered_set<int> savedTransformIds;
    if (wantJobs) {
        savedTransformIds.reserve(this->m_savedTransforms.size() + 64);
        for (auto const& saved : this->m_savedTransforms) {
            savedTransformIds.insert(saved.uniqueId);
        }
    }

    for (auto* effect : followEffects) {
        if (this->m_dynamicFollowJobs.size() >= kMaxDynamicFollowJobs) break;

        // Rotate-follow / non-positional: mark targets Unknown, no cheap probe.
        bool const rotateOnly = effect->m_rotateFollowP1 || effect->m_rotateFollowP2;
        bool const followY = TriggerClassify::isFollowPlayerTriggerId(effect->m_objectID);
        bool const targetPlayer = effect->m_targetPlayer1 || effect->m_targetPlayer2;
        bool const lockX = effect->m_lockToPlayerX || targetPlayer;
        bool const lockY = effect->m_lockToPlayerY || targetPlayer || followY;
        bool const probeable = !rotateOnly && (lockX || lockY);

        int const groupId = effect->m_targetGroupID;
        if (groupId <= 0) continue;

        DynamicFollowJob job;
        job.effectUniqueId = effect->m_uniqueID;
        job.effect = effect;
        job.lockX = lockX;
        job.lockY = lockY;
        job.followPlayerY = followY;
        job.offsetX = effect->m_moveOffset.x;
        job.offsetY = followY
            ? static_cast<float>(effect->m_followYOffset)
            : effect->m_moveOffset.y;

        for (auto* obj : solids) {
            if (!obj || !obj->belongsToGroup(groupId)) continue;
            if (obj == effect) continue;
            this->m_dynamicTargetIds.insert(obj->m_uniqueID);

            if (!wantJobs || !probeable) continue;
            if (job.targets.size() >= kMaxDynamicTargetsPerJob) break;

            SavedTransform st;
            st.uniqueId = obj->m_uniqueID;
            st.obj = obj;
            st.x = obj->getPositionX();
            st.y = obj->getPositionY();
            st.rotation = obj->getRotation();
            st.scaleX = obj->getScaleX();
            st.scaleY = obj->getScaleY();
            st.nodeVisible = obj->isVisible();
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
    if (!layer || !ghost) return;
    if (!StageConfig::triggerBake()) return;
    if (StageConfig::triggerBakeSkipDynamic()) return;
    // Dual follow is ambiguous (P1 vs P2) — leave Unknown.
    if (layer->m_gameState.m_isDualMode && layer->m_player2) return;
    if (this->m_dynamicFollowJobs.empty()) return;

    float const gx = ghost->getPositionX();
    float const gy = ghost->getPositionY();
    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;

    for (auto& job : this->m_dynamicFollowJobs) {
        if (applied >= kMaxApplyPerTick) break;
        auto* effect = job.effect;
        if (!effect || effect->m_uniqueID != job.effectUniqueId) continue;

        for (auto const& t : job.targets) {
            if (applied >= kMaxApplyPerTick) break;
            auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wall0
            ).count();
            if (us >= kApplyBudgetUs) break;

            auto* obj = t.obj;
            if (!obj || obj->m_uniqueID != t.uniqueId) continue;
            // Prefer targets near the ghost; far follow targets stay Unknown.
            if (std::fabs(obj->getPositionX() - gx) > kApplyMarginX) continue;

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
            TriggerWorldStore::writePose(obj, pose);
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
    if (this->m_savedTransforms.empty()) return;

    for (auto const& st : this->m_savedTransforms) {
        if (!st.dirty) continue;
        st.dirty = false;
        auto* obj = st.obj;
        if (!obj || obj->m_uniqueID != st.uniqueId) continue;

        obj->setPosition({ st.x, st.y });
        obj->m_positionX = st.x;
        obj->m_positionY = st.y;
        obj->setRotation(st.rotation);
        obj->setScaleX(st.scaleX);
        obj->setScaleY(st.scaleY);
        obj->setVisible(st.nodeVisible);
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
    this->m_dynamicFollowJobs.clear();
    this->m_liveDynamicAppliedIds.clear();
    this->m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;
    this->captureTriggerTransforms(layer);
    this->captureDynamicFollows(layer);
}

void ShadowSimulator::resetCaptureForNextRun(PlayLayer* layer) {
    this->resetTriggerTransforms();
    this->m_triggerWorldAppliedThisTick = false;
    this->m_liveDynamicAppliedIds.clear();

    if (!this->m_saved.empty()) {
        for (auto const& s : this->m_saved) {
            if (!s.obj) continue;
            s.obj->m_activated = s.activated;
            s.obj->m_activatedByPlayer1 = s.p1;
            s.obj->m_activatedByPlayer2 = s.p2;
            s.obj->m_poweredOn = s.poweredOn;
            s.obj->m_state = s.state;
            s.obj->m_isActivated = s.isActivated;
            s.obj->m_visible = s.egoVisible;
            s.obj->setOpacity(s.opacity);
            s.obj->setVisible(s.nodeVisible);
            if (s.isRing) static_cast<RingObject*>(s.obj)->m_claimTouch = s.claimTouch;
        }
        this->m_saved.clear();
        if (layer) {
            layer->m_gameState.m_activatedObjectIDs = this->m_savedActivatedIDs;
        }
    }
}

void ShadowSimulator::restoreCapture(PlayLayer* layer) {
    {
        LagProbe::Scope const scope("eb.trigger.restore");
        this->restoreTriggerTransforms(layer);
    }
    this->m_dynamicFollowJobs.clear();
    this->m_dynamicTargetIds.clear();
    this->m_liveDynamicAppliedIds.clear();
    this->m_triggerWorldAppliedThisTick = false;

    for (auto const& s : this->m_saved) {
        if (!s.obj) continue;
        s.obj->m_activated = s.activated;
        s.obj->m_activatedByPlayer1 = s.p1;
        s.obj->m_activatedByPlayer2 = s.p2;
        s.obj->m_poweredOn = s.poweredOn;
        s.obj->m_state = s.state;
        s.obj->m_isActivated = s.isActivated;
        s.obj->m_visible = s.egoVisible;
        s.obj->setOpacity(s.opacity);
        s.obj->setVisible(s.nodeVisible);
        if (s.isRing) static_cast<RingObject*>(s.obj)->m_claimTouch = s.claimTouch;
    }
    this->m_saved.clear();
    layer->m_gameState.m_activatedObjectIDs = this->m_savedActivatedIDs;
}

void ShadowSimulator::copyWaveTrail(PlayerObject* dst, PlayerObject* src) {
    auto* d = dst->m_waveTrail;
    auto* s = src->m_waveTrail;
    if (!d || !s) return;
    d->reset();
    d->m_currentPoint = s->m_currentPoint;
    d->m_waveSize = s->m_waveSize;
    d->m_pulseSize = s->m_pulseSize;
    d->m_isSolid = s->m_isSolid;
    d->m_isFlipped = s->m_isFlipped;
    d->m_drawStreak = s->m_drawStreak;
    if (!s->m_pointArray) return;
    for (unsigned i = 0; i < s->m_pointArray->count(); ++i) {
        if (auto* node = static_cast<PointNode*>(s->m_pointArray->objectAtIndex(i))) {
            d->addPoint(node->m_point);
        }
    }
}

void ShadowSimulator::clearCollisionLogs(PlayerObject* s) {
    s->m_collisionLogTop->removeAllObjects();
    s->m_collisionLogBottom->removeAllObjects();
    s->m_collisionLogLeft->removeAllObjects();
    s->m_collisionLogRight->removeAllObjects();
    s->m_lastCollisionTop = -1;
    s->m_lastCollisionBottom = -1;
    s->m_lastCollisionLeft = -1;
    s->m_lastCollisionRight = -1;
}

void ShadowSimulator::applyTriggerWorldAtTick(
    PlayLayer* layer, int absTick, PlayerObject* ghost
) {
    this->m_triggerWorldAppliedThisTick = false;
    if (!layer || absTick < 0) return;
    if (!StageConfig::triggerBake()) return;
    if (this->m_savedTransforms.empty()) return;

    auto& store = TriggerWorldStore::get();
    if (!store.loaded() || !store.hasTracks()) return;

    LagProbe::Scope const lagScope("eb.trigger.apply");

    float const gx = ghost ? ghost->getPositionX() : 0.f;
    struct Cand {
        SavedTransform const* st = nullptr;
        float dist = 0.f;
        // Named isNear — Windows headers define `near` as a macro.
        bool isNear = false;
    };
    std::vector<Cand> cands;
    cands.reserve(128);
    constexpr float kCullMarginX = kApplyMarginX * 2.5f;
    for (auto const& st : this->m_savedTransforms) {
        auto* obj = st.obj;
        if (!obj || obj->m_uniqueID != st.uniqueId) continue;
        float const distNow = std::fabs(obj->getPositionX() - gx);
        float const distBase = std::fabs(st.x - gx);
        float const dist = std::min(distNow, distBase);
        if (dist > kCullMarginX) continue;
        Cand c;
        c.st = &st;
        c.dist = dist;
        c.isNear = dist <= kApplyMarginX;
        cands.push_back(c);
    }
    // Near objects first, then closer far objects — skip remainder if budget/cap.
    std::sort(cands.begin(), cands.end(), [](Cand const& a, Cand const& b) {
        if (a.isNear != b.isNear) return a.isNear && !b.isNear;
        return a.dist < b.dist;
    });

    auto const wall0 = std::chrono::steady_clock::now();
    size_t applied = 0;
    for (auto const& c : cands) {
        if (applied >= kMaxApplyPerTick) break;
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall0
        ).count();
        if (us >= kApplyBudgetUs) break;

        auto* obj = c.st->obj;
        TriggerPose pose{};
        if (!store.samplePose(c.st->uniqueId, absTick, pose)) continue;
        TriggerWorldStore::writePose(obj, pose);
        c.st->dirty = true;
        ++applied;
    }
    // Far / over-budget tracks hold last written pose (or capture baseline).
    this->m_triggerWorldAppliedThisTick = true;
}

void ShadowSimulator::stepPreCollide(PlayLayer* layer, PlayerObject* s, float dt) {
    // Always use the full step dt here — tiny CBF remainders launch slopes
    // and also under-detect orb/spike overlaps.
    this->m_triggerWorldAppliedThisTick = false;
    this->m_liveDynamicAppliedIds.clear();
    if (this->m_simAbsTick >= 0) {
        this->applyTriggerWorldAtTick(layer, this->m_simAbsTick, s);
        this->applyLiveDynamicNear(layer, s);
    }
    float const gdDt = AssistClock::secondsToGdStep(dt);
    hideGhost(s);
    clearCollisionLogs(s);
    layer->checkCollisions(s, gdDt, false);
    hideGhost(s);
}

void ShadowSimulator::stepPhysics(PlayLayer* layer, PlayerObject* s, float stepDt, float dt) {
    this->m_triggerWorldAppliedThisTick = false;
    this->m_liveDynamicAppliedIds.clear();
    if (this->m_simAbsTick >= 0) {
        this->applyTriggerWorldAtTick(layer, this->m_simAbsTick, s);
        this->applyLiveDynamicNear(layer, s);
    }
    float const gdStepDt = AssistClock::secondsToGdStep(stepDt);
    float colDt = stepDt;
    if (colDt < dt) colDt = dt; // same rule as pre-collide: never a tiny remainder
    float const gdColDt = AssistClock::secondsToGdStep(colDt);
    hideGhost(s);
    s->update(gdStepDt);
    hideGhost(s);
    if (this->m_dead) return;
    clearCollisionLogs(s);
    layer->checkCollisions(s, gdColDt, false);
    hideGhost(s);
}

#define EB_COPY(field) dst->field = src->field

void ShadowSimulator::copyPlayerState(PlayerObject* dst, PlayerObject* src) {
    if (!dst || !src) return;

    dst->copyAttributes(src);
    dst->setPosition(src->getPosition());
    dst->setRotation(src->getRotation());
    dst->setScaleX(src->getScaleX());
    dst->setScaleY(src->getScaleY());
    hideGhost(dst);

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
    dst->m_isDead = false;

    if (dst->m_touchingRings) {
        dst->m_touchingRings->removeAllObjects();
        if (src->m_touchingRings) {
            for (unsigned i = 0; i < src->m_touchingRings->count(); ++i) {
                dst->m_touchingRings->addObject(src->m_touchingRings->objectAtIndex(i));
            }
        }
    }

    clearCollisionLogs(dst);

    if (src->m_isDart) {
        // local helper via instance method — static caller uses get()
        get().copyWaveTrail(dst, src);
    }
}

#undef EB_COPY

int ShadowSimulator::captureSnapshot(PlayerObject* real) {
    if (!real || !m_snapshots[0]) return -1;
    int const slot = m_nextSnapshot;
    m_nextSnapshot = (m_nextSnapshot + 1) % SNAPSHOT_SLOTS;
    copyPlayerState(m_snapshots[slot], real);
    return slot;
}

PlayerObject* ShadowSimulator::snapshotSlot(int slot) const {
    if (slot < 0 || slot >= SNAPSHOT_SLOTS) return nullptr;
    return m_snapshots[slot];
}

ShadowSimulator::Model ShadowSimulator::modelFor(PlayerObject* p, int limit, float capMul) {
    Model m;
    if (!p) return m;
    auto const params = StageConfig::paramsFor(p);
    m.slack = std::max(0, params.slack);
    int const x = limit > 0 ? limit : 1;
    if (capMul < 0.25f) capMul = 0.25f;
    if (capMul > 3.f) capMul = 3.f;

    auto horizon = [&](int automatic) {
        int base = params.cap > 0 ? params.cap : automatic;
        return std::max(8, static_cast<int>(std::lround(base * capMul)));
    };

    if (p->m_isDart) {
        m.continuous = true;
        m.cap = horizon(std::max(32, x + 16));
    }
    else if (p->m_isShip || p->m_isSwing) {
        m.continuous = true;
        m.cap = horizon(std::max(64, x * 3));
    }
    else if (p->m_isBird) {
        m.continuous = true;
        m.tapRelease = true;
        m.cap = horizon(std::max(40, x + 20));
    }
    else if (p->m_isSpider) {
        m.continuous = true;
        m.tapRelease = true;
        m.cap = horizon(std::max(36, x + 16));
    }
    else if (p->m_isRobot) {
        m.tapRelease = false;
        m.cap = horizon(150);
    }
    else {
        m.tapRelease = true;
        m.cap = horizon(150);
    }
    return m;
}

bool ShadowSimulator::isHazardLike(GameObject* obj) {
    if (!obj) return false;
    switch (obj->m_objectType) {
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
        if (half < 12.f) half = 12.f;
        z.startX = std::min(z.startX, x - half);
        z.endX = std::max(z.endX, x + half);
    }
    if (!layer || !layer->m_objectLayer) return z;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return z;

    struct HazardSpan {
        float lo = 0.f;
        float hi = 0.f;
    };
    std::vector<HazardSpan> localHazards;
    localHazards.reserve(128);
    constexpr float kMaxClusterRadiusX = 800.f;
    float const winLo = z.startX - kMaxClusterRadiusX;
    float const winHi = z.endX + kMaxClusterRadiusX;
    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!isHazardLike(obj)) continue;
        float const x = obj->getPositionX();
        if (x < winLo || x > winHi) continue;
        float half = obj->getContentSize().width * std::fabs(obj->getScaleX()) * 0.5f;
        if (half < 10.f) half = 10.f;
        localHazards.push_back({ x - half, x + half });
    }

    bool grew = true;
    int guard = 0;
    while (grew && guard++ < 32) {
        grew = false;
        for (auto const& h : localHazards) {
            if (h.hi + kClusterGapX < z.startX || h.lo - kClusterGapX > z.endX) continue;
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

bool ShadowSimulator::spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit, float dt) {
    if (!layer || !real || dt <= 0.f) return false;
    int slack = 0;
    if (real) {
        slack = StageConfig::paramsFor(real).slack;
    }
    // Dt-/speed-aware orb + jump-pad cone (replaces fixed (limit+8)*5.5).
    return TravelForesight::hasOrbOrPadAhead(layer, real, limit, dt, slack);
}

bool ShadowSimulator::spatialHazardProximity(
    PlayLayer* layer, PlayerObject* real, float proximityX
) {
    if (!layer || !real || !layer->m_objectLayer || proximityX <= 0.f) return false;

    float const px = real->getPositionX();
    float const py = real->getPositionY();
    float const yReach = real->m_isShip || real->m_isDart || real->m_isSwing ? 100.f : 70.f;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return false;

    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!isHazardLike(obj)) continue;

        float const ox = obj->getPositionX();
        float const oy = obj->getPositionY();
        float half = obj->getContentSize().width * std::fabs(obj->getScaleX()) * 0.5f;
        if (half < 8.f) half = 8.f;
        float const oLo = ox - half;
        float const oHi = ox + half;
        // Ahead of player, overlapping [px, px+proximityX].
        if (oHi < px - 4.f) continue;
        if (oLo > px + proximityX) continue;
        if (std::fabs(oy - py) > yReach) continue;
        return true;
    }
    return false;
}

bool ShadowSimulator::shouldWaveHazardProximityPass(
    bool isWave, bool nearHazard, bool noOrbTarget, bool foresightOrbPad
) {
    if (!isWave) return false;
    if (!nearHazard) return false;
    if (!noOrbTarget) return false;
    // Miss-assist: orb/pad in travel cone → keep searching, do not Pass.
    if (foresightOrbPad) return false;
    return true;
}

bool ShadowSimulator::shouldWaveFutileDelayPass(
    bool isWave,
    bool orbIntent,
    int bestSurviveBin,
    float bestEndXGain,
    bool foresightOrbPad
) {
    if (!isWave) return false;
    if (orbIntent) return false;
    if (foresightOrbPad) return false;
    if (bestSurviveBin > 0) return false;
    if (bestEndXGain >= kFutileEndXGain) return false;
    return true;
}

bool ShadowSimulator::deathLooksLikeAscent(
    float playerY, float deathY, bool upsideDown, float marginY
) {
    if (marginY < 0.f) marginY = 0.f;
    if (upsideDown) {
        // Flipped: "ceiling" is below in world Y.
        return deathY < playerY - marginY;
    }
    return deathY > playerY + marginY;
}

bool ShadowSimulator::classifyIdleSafer(
    bool continuous,
    bool pressDown,
    bool baselineDied,
    int baselineDeathTick,
    float baselineDeathX,
    bool d0Died,
    int d0DeathTick,
    float d0DeathX
) {
    if (!continuous || !pressDown) return false;
    // Idle must look better than pressing now.
    if (!d0Died) return false;
    if (!baselineDied) return true;
    if (baselineDeathTick < 0 || d0DeathTick < 0) return !baselineDied;
    if (baselineDeathTick > d0DeathTick) return true;
    if (baselineDeathTick == d0DeathTick && baselineDeathX > d0DeathX + 8.f) {
        return true;
    }
    return false;
}

bool ShadowSimulator::classifyAscentUseful(
    bool continuous,
    bool pressDown,
    bool d0Viable,
    int d0SurviveBin,
    float d0EndX,
    bool laterViable,
    int laterSurviveBin,
    float laterEndX
) {
    if (!continuous || !pressDown) return false;
    if (laterViable && !d0Viable) return true;
    if (laterSurviveBin > d0SurviveBin) return true;
    if (laterViable && laterEndX > d0EndX + kFutileEndXGain) return true;
    return false;
}

int ShadowSimulator::pickPreferredRelease(int earliest, int latest, bool continuous) {
    if (earliest < 0) return -1;
    if (latest < earliest) latest = earliest;
    if (continuous) return earliest; // wave/ship: let go ASAP inside safe window
    return earliest + (latest - earliest) / 2;
}

int ShadowSimulator::autoRobustKHints(
    bool waveLike,
    bool isWave,
    float playerSpeed,
    float dtVariance,
    bool spamDense
) {
    int k = kWindowMarginK; // 1
    if (waveLike) k = 2;
    // Wave spam corridors: keep at least 2; at speed bump to 3 so ±1 neighbors
    // of the pick stay clear (Tight ticks-wave=2 may still fall back — see docs).
    if (isWave && spamDense) {
        k = std::max(k, 2);
        if (playerSpeed > 1.05f) k = std::max(k, 3);
    }
    if (playerSpeed > 1.1f) k = std::max(k, 2);
    if (playerSpeed > 1.5f) k = 3;
    if (dtVariance > 0.08f) k = std::min(3, k + 1);
    return std::clamp(k, 1, 3);
}

int ShadowSimulator::autoRobustK(
    PlayerObject* real, float dtVariance, bool spamDense
) {
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

ShadowSimulator::RunResult ShadowSimulator::runOne(
    PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
    float dt, float firstDt, Model const& model, int releaseAfter,
    ThreatZone const* zone, int* tickBudgetLeft
) {
    RunResult r;
    (void)limit;
    copyPlayerState(m_shadow, real);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* s = m_shadow;
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    int groundedStreak = 0;
    int settledAfterClear = 0;
    bool pastZone = false;
    int const maxTicks = std::max(std::max(0, limit), delay) + std::max(1, model.cap);

    auto finish = [&](RunResult out) {
        out.activatedRing = m_activatedRing;
        out.ringCount = m_ringCount;
        out.endX = s->getPositionX();
        out.wasDashing = s->m_isDashing;
        out.ticksRan = std::max(out.ticksRan, out.deathTick >= 0 ? out.deathTick + 1 : 0);
        if (out.ticksBeforeNeutral <= 0) out.ticksBeforeNeutral = out.ticksRan;
        return out;
    };

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft) return false;
        if (*tickBudgetLeft <= 0) return true;
        --(*tickBudgetLeft);
        return false;
    };

    if (delay > 0) {
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase;
        stepPreCollide(layer, s, dt);
        if (m_dead) {
            r.ticksRan = 1;
            r.died = true;
            r.deathTick = 0;
            r.deathX = s->getPositionX();
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

    for (int t = 0; t < maxTicks; ++t) {
        if (spend()) break;
        float const stepDt = (t == 0) ? firstDt : dt;
        r.ticksRan = t + 1;
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase + t;

        if (!acted && t == delay) {
            acted = true;
            if (down) {
                s->pushButton(PlayerButton::Jump);
                s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            }
            else {
                s->releaseButton(PlayerButton::Jump);
                s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        }
        else if (acted && down) {
            if (releaseAfter >= 0 && !tapReleased && t == delay + releaseAfter) {
                tapReleased = true;
                s->releaseButton(PlayerButton::Jump);
            }
            else if (releaseAfter < 0 && model.tapRelease && !tapReleased && m_jumped) {
                bool const touchingOrb = m_orbOverlapTick
                    || (s->m_touchingRings && s->m_touchingRings->count() > 0);
                bool const dashing = s->m_isDashing;
                bool const justHitOrb = m_activatedRing && t <= delay + 4;
                if (!touchingOrb && !dashing && !justHitOrb) {
                    tapReleased = true;
                    s->releaseButton(PlayerButton::Jump);
                }
            }
        }

        stepPhysics(layer, s, stepDt, dt);
        if (m_dead) {
            r.died = true;
            r.deathTick = t;
            r.deathX = s->getPositionX();
            r.deathObject = m_deathObject;
            r.diedBeforeAction = !acted;
            return finish(r);
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = t;
            return finish(r);
        }

        if (m_jumped && !r.jumped) {
            r.jumped = true;
            r.jumpTick = t;
        }
        if (s->m_isDashing) r.wasDashing = true;

        // G2: after threat zone + settled → neutral (stop extending survive score)
        if (zone && zone->valid && !pastZone) {
            if (s->getPositionX() > zone->endX) {
                bool settled = false;
                if (model.continuous) {
                    settledAfterClear++;
                    settled = settledAfterClear >= 8;
                }
                else {
                    bool const grounded = s->m_isOnGround || s->m_isOnGround2;
                    if (grounded) ++groundedStreak;
                    else groundedStreak = 0;
                    settled = groundedStreak >= 2;
                }
                if (settled) {
                    pastZone = true;
                    r.zoneCleared = true;
                    r.ticksBeforeNeutral = t + 1;
                    // Keep simulating briefly for orb/path info but mark neutral
                }
            }
            else {
                settledAfterClear = 0;
            }
        }

        if (pastZone && t + 1 - r.ticksBeforeNeutral > 24) {
            return finish(r);
        }
    }
    return finish(r);
}

ShadowSimulator::RunResult ShadowSimulator::runBaseline(
    PlayLayer* layer, PlayerObject* real, bool down,
    float dt, float firstDt, Model const& model, int* tickBudgetLeft
) {
    RunResult r;
    copyPlayerState(m_shadow, real);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* s = m_shadow;
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    int const maxTicks = std::max(1, model.cap);
    for (int t = 0; t < maxTicks; ++t) {
        if (tickBudgetLeft) {
            if (*tickBudgetLeft <= 0) break;
            --(*tickBudgetLeft);
        }
        float const stepDt = (t == 0) ? firstDt : dt;
        this->m_simAbsTick = this->m_simTickBase + t;
        stepPhysics(layer, s, stepDt, dt);
        r.ticksRan = t + 1;
        r.endX = s->getPositionX();
        if (m_dead) {
            r.died = true;
            r.deathTick = t;
            r.deathX = s->getPositionX();
            r.deathObject = m_deathObject;
            return r;
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = t;
            return r;
        }
    }
    return r;
}

ShadowSimulator::OrbWindow ShadowSimulator::scanOrbWindow(
    PlayLayer* layer, PlayerObject* real, bool down,
    int scanTicks, float dt, float firstDt, int* tickBudgetLeft,
    bool holdJump
) {
    OrbWindow win;
    if (!layer || !real || !this->m_shadow || scanTicks <= 0) return win;

    this->copyPlayerState(this->m_shadow, real);
    this->m_dead = false;
    this->m_jumped = false;
    this->m_unknown = false;
    this->m_activatedRing = false;
    this->m_ringCount = 0;
    this->m_orbOverlapTick = false;
    this->m_deathObject = nullptr;

    auto* s = this->m_shadow;
    // Press-to-reach: hold Jump. Otherwise legacy coast with Jump = !down.
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] =
        holdJump ? true : !down;

    m_orbScanning = true;
    for (int t = 0; t < scanTicks; ++t) {
        if (tickBudgetLeft) {
            if (*tickBudgetLeft <= 0) break;
            --(*tickBudgetLeft);
        }
        float const stepDt = (t == 0) ? firstDt : dt;
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase + t;

        stepPreCollide(layer, s, dt);
        bool const touchPre = m_orbOverlapTick
            || (s->m_touchingRings && s->m_touchingRings->count() > 0);
        if (touchPre) {
            if (!win.found) {
                win.found = true;
                win.first = t;
            }
            win.last = t;
        }
        if (m_dead || m_unknown) break;

        m_orbOverlapTick = false;
        stepPhysics(layer, s, stepDt, dt);
        bool const touchPost = m_orbOverlapTick
            || (s->m_touchingRings && s->m_touchingRings->count() > 0);
        if (touchPost) {
            if (!win.found) {
                win.found = true;
                win.first = t;
            }
            win.last = t;
        }
        if (m_dead || m_unknown) break;
    }
    m_orbScanning = false;
    return win;
}


struct Cand {
    int delay = 0;
    bool viable = false;
    bool diedBeforeAction = false;
    bool dualFailed = false;
    bool clearsThreat = false;
    bool unknownTrunc = false;
    bool activatedRing = false;
    bool orbUseful = false;
    bool requireHold = false;
    bool robustKFailed = false;
    bool verifyFailed = false;
    int releaseAfter = -1; // ticks after press; -1 = auto tap / unknown
    int surviveBin = 0;
    int surviveTicks = 0;
    float endX = 0.f;
    float deathX = 0.f;
    int jumpTick = 0;
    ShadowSimulator::RunResult raw{};
};

static int surviveBinOf(int ticks) {
    if (ticks < 24) return 0;
    if (ticks < 48) return 1;
    if (ticks < 96) return 2;
    return 3;
}

static bool clearsThreatOf(ShadowSimulator::RunResult const& r, ThreatZone const& zone) {
    if (!zone.valid) {
        if (r.unknownTrunc) return true;
        return !r.died;
    }
    if (!r.died) return r.endX > zone.endX || r.zoneCleared;
    return r.deathX > zone.endX;
}

// New (preferOutcome): rank by predicted exit — surviveBin/ticks, then
// endX − λ·delay (how far the jump carries). No fixed mid/latest.
static bool betterCand(
    Cand const& a, Cand const& b, float latencyWeight, bool preferOutcome
) {
    if (a.viable != b.viable) return a.viable && !b.viable;
    if (a.orbUseful != b.orbUseful) return a.orbUseful && !b.orbUseful;
    if (preferOutcome && a.raw.died != b.raw.died) {
        return !a.raw.died && b.raw.died;
    }
    if (a.surviveBin != b.surviveBin) return a.surviveBin > b.surviveBin;
    if (a.surviveTicks != b.surviveTicks) return a.surviveTicks > b.surviveTicks;
    float const scoreA = a.endX - latencyWeight * static_cast<float>(a.delay);
    float const scoreB = b.endX - latencyWeight * static_cast<float>(b.delay);
    if (scoreA != scoreB) return scoreA > scoreB;
    // Weak early bias when prediction ties (gap-jump height).
    return a.delay < b.delay;
}

// 0 = auto from mode / speed / dt variance / spam density; else clamp 1–3.
static int resolveRobustK(PlayerObject* real, PlanOptions const& options) {
    int forced = options.robustK;
    if (forced <= 0) forced = StageConfig::robustK();
    if (forced > 0) return std::clamp(forced, 1, 3);
    return ShadowSimulator::autoRobustK(real, options.dtVariance, options.spamDense);
}



ShadowSimulator::VerifyDelayResult ShadowSimulator::verifyDelay(
    PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    VerifyDelayResult out;
    if (!layer || !real || !m_shadow || limit < 0 || dt <= 0.f) return out;
    if (firstDt <= 0.f || firstDt > dt) firstDt = dt;
    delay = std::max(0, delay);

    Model model = modelFor(real, limit, options.capMul);
    int tickBudget = options.tickBudget > 0 ? options.tickBudget : kDefaultTickBudget;
    int const tickBudgetStart = tickBudget;
    auto const wall0 = std::chrono::steady_clock::now();
    auto wallExceeded = [&]() -> bool {
        if (options.budgetUs <= 0) return false;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall0
        ).count();
        return us >= options.budgetUs;
    };
    auto fillSpent = [&]() {
        out.ticksSpent = std::max(0, tickBudgetStart - tickBudget);
        out.usSpent = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall0
        ).count());
    };

    auto linkedDual = [&]() -> bool {
        if (!layer || !layer->m_player2 || !real) return false;
        if (!layer->m_gameState.m_isDualMode) return false;
        if (auto* gm = GameManager::sharedState()) {
            if (gm->getGameVariable("0010")) return false;
        }
        return real == layer->m_player1 || real == layer->m_player2;
    };

    this->m_simTickBase = std::max(0, options.attemptStartTick);
    this->m_simAbsTick = -1;
    this->m_simulating = true;
    this->beginCapture(layer);

    ThreatZone zone{};
    // Probe d0 to build the same threat zone Live uses. When delay==0 the
    // planned candidate is identical — reuse that runOne (no second sim).
    auto c0 = this->runOne(
        layer, real, down, 0, limit, dt, firstDt, model, -1,
        nullptr, &tickBudget
    );
    this->resetCaptureForNextRun(layer);
    if (c0.died) {
        zone = buildThreatZone(layer, c0.deathX, c0.deathObject);
        out.threatZone = zone;
    }
    if (tickBudget <= 0 || wallExceeded()) {
        out.budgetHit = true;
        this->m_simAbsTick = -1;
        this->restoreCapture(layer);
        this->m_simulating = false;
        this->parkGhosts();
        fillSpent();
        return out;
    }

    RunResult r;
    if (delay == 0) {
        r = c0;
    }
    else {
        r = this->runOne(
            layer, real, down, delay, limit, dt, firstDt, model, -1,
            zone.valid ? &zone : nullptr, &tickBudget
        );
        this->resetCaptureForNextRun(layer);
    }

    out.diedBeforeAction = r.diedBeforeAction;
    out.clearsThreat = clearsThreatOf(r, zone);
    out.unknown = r.unknown || r.unknownTrunc;
    // Survive-only: never ok a bake delay that dies in sim.
    bool viable = false;
    if (r.diedBeforeAction || r.died) {
        viable = false;
    }
    else if (r.unknownTrunc) {
        viable = !zone.valid || out.clearsThreat || r.endX > zone.endX;
    }
    else {
        viable = !zone.valid || out.clearsThreat || r.zoneCleared;
    }

    // Linked dual: still one runOne for the other player (even when delay==0).
    if (viable && linkedDual()) {
        PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
        if (other && tickBudget > 0 && !wallExceeded()) {
            auto r2 = this->runOne(
                layer, other, down, delay, limit, dt, firstDt, model, -1,
                zone.valid ? &zone : nullptr, &tickBudget
            );
            this->resetCaptureForNextRun(layer);
            if (r2.died || r2.diedBeforeAction || !clearsThreatOf(r2, zone)) {
                viable = false;
                out.clearsThreat = false;
            }
        }
    }

    if (tickBudget <= 0 || wallExceeded()) out.budgetHit = true;

    this->m_simAbsTick = -1;
    this->restoreCapture(layer);
    this->m_simulating = false;
    this->parkGhosts();

    fillSpent();
    // Budget exhaustion → do not trust bake (caller falls through / Pass).
    out.ok = viable && !out.budgetHit;
    return out;
}

SimPlan ShadowSimulator::plan(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    PerfTrace::Scope const scope("plan");
    static int s_planId = 1;
    SimPlan plan;
    plan.smartDepthUsed = 1;
    plan.planId = s_planId++;
    if (!layer || !real || !this->m_shadow || limit < 0 || dt <= 0.f) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassInvalidArgs;
        if (limit >= 0) {
            DevRejectLedger::BuildInput emptyIn;
            emptyIn.limit = limit;
            emptyIn.doPass = true;
            emptyIn.chosenDelay = 0;
            emptyIn.includeScores = DevConfig::recordCands();
            auto led = DevRejectLedger::build(emptyIn);
            plan.candNotes = std::move(led.notes);
            plan.candNotesTruncated = led.truncated;
        }
        return plan;
    }
    if (firstDt <= 0.f || firstDt > dt) firstDt = dt;

    Model model = modelFor(real, limit, options.capMul);
    plan.continuous = model.continuous;
    int const robustK = resolveRobustK(real, options);
    float const latencyWeight = std::max(0.f, options.latencyWeight);

    int tickBudget = options.tickBudget > 0 ? options.tickBudget : kDefaultTickBudget;
    // A/B compare runs both arms' semantics over the same cand set; allow 2× ticks.
    if (StageConfig::assistVerifyAb()) tickBudget *= 2;
    auto const wall0 = std::chrono::steady_clock::now();
    auto wallExceeded = [&]() -> bool {
        if (options.budgetUs <= 0) return false;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall0
        ).count();
        return us >= options.budgetUs;
    };

    auto linkedDual = [&]() -> bool {
        if (!layer || !layer->m_player2 || !real) return false;
        if (!layer->m_gameState.m_isDualMode) return false;
        // "0010" Flip2PlayerControls → split buffers; linked = shared jump for both.
        if (auto* gm = GameManager::sharedState()) {
            if (gm->getGameVariable("0010")) return false;
        }
        return real == layer->m_player1 || real == layer->m_player2;
    };

    this->m_simTickBase = std::max(0, options.attemptStartTick);
    this->m_simAbsTick = -1;
    m_simulating = true;
    beginCapture(layer);

    {
        auto base = runBaseline(layer, real, down, dt, firstDt, model, &tickBudget);
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
    if (orbAssist) {
        int const scanTicks = OpportunityProbe::orbScanTicks(limit, model.slack);
        orbHint = this->scanOrbWindow(
            layer, real, down, scanTicks, dt, firstDt, &tickBudget, false
        );
        this->resetCaptureForNextRun(layer);
        // Press-to-reach: second scan with Jump held if coast missed (budget-capped).
        if (!orbHint.found && tickBudget >= OpportunityProbe::kMinBudgetForProbe) {
            int const reachTicks = OpportunityProbe::pressReachScanTicks(
                limit, model.slack
            );
            auto held = this->scanOrbWindow(
                layer, real, down, reachTicks, dt, firstDt, &tickBudget, true
            );
            this->resetCaptureForNextRun(layer);
            if (held.found) {
                orbHint = held;
            }
        }
        if (orbHint.found) plan.usedOrbWindow = true;
        spatialHint = this->spatialOrbHint(layer, real, limit, dt);
        plan.foresightOrbPad = spatialHint;
    }

    ThreatZone zone{};
    std::vector<Cand> cands(static_cast<size_t>(limit) + 1);
    std::vector<char> candReady(static_cast<size_t>(limit) + 1, 0);

    int ledgerActMin = -1;
    int ledgerActMax = -1;
    bool ledgerOrbIntent = false;
    bool ledgerPreferEarlyExit = false;

    auto publishLedger = [&](
        int chosenDelay,
        bool doPassFlag,
        DevRejectLedger::EarlyExit early
    ) {
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
        for (int d = 0; d <= limit; ++d) {
            if (!candReady[static_cast<size_t>(d)]) continue;
            auto const& c = cands[static_cast<size_t>(d)];
            auto& s = in.snaps[static_cast<size_t>(d)];
            s.evaluated = true;
            s.viable = c.viable;
            s.diedBeforeAction = c.diedBeforeAction;
            s.dualFailed = c.dualFailed;
            s.unknownTrunc = c.unknownTrunc;
            s.clearsThreat = c.clearsThreat;
            s.activatedRing = c.activatedRing;
            s.robustKFailed = c.robustKFailed;
            s.verifyFailed = c.verifyFailed;
            s.surviveBin = c.surviveBin;
            s.endX = c.endX;
        }
        auto led = DevRejectLedger::build(in);
        plan.candNotes = std::move(led.notes);
        plan.candNotesTruncated = led.truncated;
    };

    auto evalDelay = [&](int d, int releaseAfter) -> Cand {
        Cand c;
        c.delay = d;
        if (tickBudget <= 0 || wallExceeded()) {
            plan.budgetHit = true;
            return c;
        }
        auto r = runOne(
            layer, real, down, d, limit, dt, firstDt, model, releaseAfter,
            zone.valid ? &zone : nullptr, &tickBudget
        );
        this->resetCaptureForNextRun(layer);
        ++plan.candidatesTried;
        c.raw = r;
        c.diedBeforeAction = r.diedBeforeAction;
        c.activatedRing = r.activatedRing;
        c.unknownTrunc = r.unknownTrunc;
        c.endX = r.endX;
        c.deathX = r.deathX;
        c.jumpTick = r.jumped ? r.jumpTick : d;
        c.surviveTicks = r.ticksBeforeNeutral > 0 ? r.ticksBeforeNeutral : r.ticksRan;
        c.surviveBin = surviveBinOf(c.surviveTicks);
        c.requireHold = false; // set only if tap fails and hold-branch wins
        return c;
    };

    auto finalizeViable = [&](Cand& c, int releaseAfter) {
        c.clearsThreat = clearsThreatOf(c.raw, zone);
        // Delay-viable ONLY if the sim survives. Death-past-zone used to count
        // as "clear" and caused +N Delay that still died (early or late).
        // Uncertain → not Delay-viable (Pass / keep searching), never assist into death.
        if (c.diedBeforeAction || c.raw.died) {
            c.viable = false;
        }
        else if (c.unknownTrunc) {
            c.viable = !zone.valid || c.clearsThreat || c.raw.endX > zone.endX;
        }
        else {
            c.viable = !zone.valid || c.clearsThreat || c.raw.zoneCleared
                || c.surviveBin >= 1;
        }
        // Linked dual: other player's shadow must also survive the same plan.
        if (c.viable && linkedDual()) {
            PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
            if (other && tickBudget > 0) {
                auto r2 = runOne(
                    layer, other, down, c.delay, limit, dt, firstDt, model, releaseAfter,
                    zone.valid ? &zone : nullptr, &tickBudget
                );
                this->resetCaptureForNextRun(layer);
                if (r2.died || r2.diedBeforeAction
                    || !clearsThreatOf(r2, zone)) {
                    c.viable = false;
                    c.clearsThreat = false;
                    c.dualFailed = true;
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
            }
            else {
                zone = buildThreatZone(layer, c0.raw.deathX, c0.raw.deathObject);
            }
            plan.threatZone = zone;
        }
        finalizeViable(c0, -1);
        cands[0] = c0;
        candReady[0] = 1;

        // Continuous: idle (baseline) safer than d0 action → miss-assist may Delay.
        plan.idleSafer = classifyIdleSafer(
            model.continuous,
            true,
            plan.baselineDeathTick >= 0,
            plan.baselineDeathTick,
            plan.baselineDeathX,
            c0.raw.died,
            c0.raw.died ? c0.raw.deathTick : -1,
            c0.raw.died ? c0.raw.deathX : c0.endX
        );

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
            auto runProbeDelay = [&](int d) {
                if (d <= 0 || d > limit) return;
                if (candReady[static_cast<size_t>(d)]) return;
                if (tickBudget < OpportunityProbe::kMinBudgetForProbe || wallExceeded()) {
                    plan.budgetHit = true;
                    return;
                }
                auto c = evalDelay(d, -1);
                if (c.diedBeforeAction) {
                    // Still record — probe may show nothing useful.
                    finalizeViable(c, -1);
                    cands[static_cast<size_t>(d)] = c;
                    candReady[static_cast<size_t>(d)] = 1;
                    if (probeCount < 2) {
                        auto& ps = probeSamples[probeCount++];
                        ps.activatedRing = c.activatedRing;
                        ps.diedBeforeAction = c.diedBeforeAction;
                        ps.surviveBin = c.surviveBin;
                        ps.endX = c.endX;
                    }
                    return;
                }
                finalizeViable(c, c.releaseAfter >= 0 ? c.releaseAfter : -1);
                cands[static_cast<size_t>(d)] = c;
                candReady[static_cast<size_t>(d)] = 1;
                if (probeCount < 2) {
                    auto& ps = probeSamples[probeCount++];
                    ps.activatedRing = c.activatedRing;
                    ps.diedBeforeAction = c.diedBeforeAction;
                    ps.surviveBin = c.surviveBin;
                    ps.endX = c.endX;
                }
            };

            if (limit >= 1) {
                runProbeDelay(mid);
                if (limit != mid) {
                    runProbeDelay(limit);
                }
            }

            bool const searchFurther = OpportunityProbe::shouldSearchFurther(
                spatialHint,
                orbHint.found,
                c0.activatedRing,
                baseSample,
                probeSamples,
                probeCount
            );

            if (!searchFurther) {
                this->m_simAbsTick = -1;
                this->restoreCapture(layer);
                this->m_simulating = false;
                this->parkGhosts();
                plan.action = PlanAction::Pass;
                // Wanted probes but could not afford any → PassBudget (C6 may Hold).
                if (plan.budgetHit && limit >= 1 && probeCount == 0) {
                    plan.reason = AssistReason::PassBudget;
                }
                else {
                    plan.reason = AssistReason::PassNoThreat;
                }
                plan.delay = 0;
                plan.nowIsSafe = true;
                plan.threatCleared = true;
                plan.jumpTick = c0.jumpTick;
                plan.activatedRing = c0.activatedRing;
                AssistLog::debug(fmt::format(
                    "PLAN id={} Pass no-threat verdict={} foresight={} probes={} budgetHit={} limit={}",
                    plan.planId, AssistVerdict::name(plan.reason),
                    plan.foresightOrbPad ? 1 : 0, probeCount, plan.budgetHit, limit
                ));
                publishLedger(
                    0,
                    true,
                    plan.budgetHit
                        ? DevRejectLedger::EarlyExit::Budget
                        : DevRejectLedger::EarlyExit::NoThreat
                );
                return plan;
            }
            AssistLog::debug(fmt::format(
                "PLAN id={} opportunity-probe search-further foresight={} probes={} orbWin={}",
                plan.planId, plan.foresightOrbPad ? 1 : 0, probeCount, orbHint.found ? 1 : 0
            ));
        }

        // Wave zigzag: hazards in proximity ahead + no orb → Pass even if d0
        // built a distant ThreatZone. Delaying phase-shifts into spikes.
        // AFTER foresight: foresightOrbPad blocks this Pass (miss-assist orb).
        bool const isWave = real && real->m_isDart;
        float const proxX = std::clamp(
            static_cast<float>(limit + 4) * 5.5f,
            kHazardProximityXMin,
            kHazardProximityXMax
        );
        bool const nearHazard = isWave && spatialHazardProximity(layer, real, proxX);
        // idleSafer or d0 death: never early-Pass when immediate action dies in sim; search delays.
        if (!c0.raw.died && !plan.idleSafer && shouldWaveHazardProximityPass(
                isWave, nearHazard, noOrbTarget, plan.foresightOrbPad
            )) {
            this->m_simAbsTick = -1;
            restoreCapture(layer);
            this->m_simulating = false;
            this->parkGhosts();
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
                plan.planId, AssistVerdict::name(plan.reason), proxX, zone.valid, plan.budgetHit
            ));
            publishLedger(0, true, DevRejectLedger::EarlyExit::WaveProximity);
            return plan;
        }
    }

    std::vector<int> order;
    order.reserve(static_cast<size_t>(limit) + 1);
    order.push_back(0);
    auto pushUnique = [&](int d) {
        if (d < 0 || d > limit) return;
        if (std::find(order.begin(), order.end(), d) == order.end()) order.push_back(d);
    };
    if (orbHint.found) {
        for (int d = orbHint.first; d <= orbHint.last; ++d) pushUnique(d);
        for (int d = std::max(0, orbHint.first - 2); d <= std::min(limit, orbHint.last + 2); ++d) {
            pushUnique(d);
        }
    }

    // Bake-prior prefer band (after d0 / orb): evaluate ±k margin so robust check works.
    int preferLo = -1;
    int preferHi = -1;
    bool hasPrefer = options.preferDelayMin >= 0
        && options.preferDelayMax >= options.preferDelayMin;
    if (hasPrefer) {
        preferLo = std::max(0, options.preferDelayMin);
        preferHi = std::min(limit, options.preferDelayMax);
        hasPrefer = preferLo <= preferHi;
    }
    int preferLastInOrder = -1;
    if (hasPrefer) {
        int const k = robustK;
        int const marginLo = std::max(0, preferLo - k);
        int const marginHi = std::min(limit, preferHi + k);
        for (int d = marginLo; d <= marginHi; ++d) pushUnique(d);
        for (int d : order) {
            if (d >= marginLo && d <= marginHi) preferLastInOrder = d;
        }
    }

    for (int d = 1; d <= limit; ++d) pushUnique(d);

    auto preferBandRobust = [&]() -> bool {
        if (!hasPrefer) return false;
        int const k = robustK;
        for (int d = preferLo; d <= preferHi; ++d) {
            auto const& c = cands[static_cast<size_t>(d)];
            if (!c.viable) continue;
            bool ok = true;
            for (int e = d - k; e <= d + k; ++e) {
                if (e < 0 || e > limit) continue;
                if (!cands[static_cast<size_t>(e)].viable) {
                    ok = false;
                    break;
                }
            }
            if (ok) return true;
        }
        for (int d = preferLo; d <= preferHi; ++d) {
            if (cands[static_cast<size_t>(d)].viable) return true;
        }
        return false;
    };

    for (int d : order) {
        if (d == 0) continue;
        if (d >= 0 && d <= limit && candReady[static_cast<size_t>(d)]) {
            continue; // already filled by OpportunityProbe
        }
        if (tickBudget <= 0 || wallExceeded()) {
            plan.budgetHit = true;
            // Remaining delays stay unevaluated → budget-not-reached in ledger.
            break;
        }
        auto c = evalDelay(d, -1);
        if (c.diedBeforeAction) {
            // RC6: mark non-viable and keep searching later delays.
            finalizeViable(c, -1);
            cands[static_cast<size_t>(d)] = c;
            if (d >= 0 && d <= limit) {
                candReady[static_cast<size_t>(d)] = 1;
            }
            continue;
        }

        if (c.raw.died && !c.diedBeforeAction && down
            && (model.continuous || real->m_isRobot) && options.maxBranches > 0) {
            int tried = 0;
            for (int k : BRANCH_RELEASES) {
                if (tried >= options.maxBranches) break;
                if (tickBudget <= 0) break;
                ++tried;
                auto br = runOne(
                    layer, real, down, d, limit, dt, firstDt, model, k,
                    zone.valid ? &zone : nullptr, &tickBudget
                );
                this->resetCaptureForNextRun(layer);
                if (!br.died || br.unknownTrunc) {
                    c.raw = br;
                    c.activatedRing = br.activatedRing;
                    c.unknownTrunc = br.unknownTrunc;
                    c.endX = br.endX;
                    c.deathX = br.deathX;
                    c.surviveTicks = br.ticksBeforeNeutral > 0 ? br.ticksBeforeNeutral : br.ticksRan;
                    c.surviveBin = surviveBinOf(c.surviveTicks);
                    c.releaseAfter = k;
                    c.requireHold = false;
                    break;
                }
            }
        }
        finalizeViable(c, c.releaseAfter >= 0 ? c.releaseAfter : -1);
        cands[static_cast<size_t>(d)] = c;

        // Early-exit once prefer±k margin is evaluated and a robust prefer delay exists.
        // New: never early-exit — full survivor band needed for outcome / exit predict.
        // Legacy: orb foresight still blocks exit so activations outside prefer stay.
        bool const preferExitOk = options.legacyPick
            && MissAssistContracts::preferBandEarlyExitAllowed(
                orbAssist, orbHint.found, spatialHint, plan.foresightOrbPad
            );
        if (hasPrefer && preferExitOk && !plan.budgetHit
            && d == preferLastInOrder && preferBandRobust()) {
            AssistLog::debug(fmt::format(
                "PLAN id={} prefer-band early-exit [{},{}] after {} cands",
                plan.planId, preferLo, preferHi, plan.candidatesTried
            ));
            ledgerPreferEarlyExit = true;
            break;
        }
        if (hasPrefer && !preferExitOk && d == preferLastInOrder
            && preferBandRobust()) {
            AssistLog::debug(fmt::format(
                "PLAN id={} prefer-band early-exit suppressed ({} ) [{},{}]",
                plan.planId,
                options.legacyPick ? "orb foresight" : "new outcome",
                preferLo, preferHi
            ));
        }
    }

    for (auto& c : cands) {
        if (!c.raw.activatedRing || !c.raw.wasDashing) continue;
        if (c.viable) continue;
        if (tickBudget <= 0) break;
        auto hold = runOne(
            layer, real, down, c.delay, limit, dt, firstDt, model, 40,
            zone.valid ? &zone : nullptr, &tickBudget
        );
        this->resetCaptureForNextRun(layer);
        Cand h = c;
        h.raw = hold;
        h.activatedRing = hold.activatedRing;
        h.unknownTrunc = hold.unknownTrunc;
        h.endX = hold.endX;
        h.deathX = hold.deathX;
        h.surviveTicks = hold.ticksBeforeNeutral > 0 ? hold.ticksBeforeNeutral : hold.ticksRan;
        h.surviveBin = surviveBinOf(h.surviveTicks);
        finalizeViable(h, 40);
        if (h.viable && !c.viable) {
            c = h;
            c.requireHold = true;
            c.releaseAfter = 40;
        }
    }

    bool anyActivation = false;
    for (auto const& c : cands) {
        if (c.activatedRing) anyActivation = true;
    }
    bool orbIntent = orbAssist && (orbHint.found || spatialHint || anyActivation);
    ledgerOrbIntent = orbIntent;

    // Continuous ascentUseful: any later delay beats d0 (viable / bin / endX).
    {
        auto const& c0ref = cands[0];
        for (int d = 1; d <= limit; ++d) {
            auto const& c = cands[static_cast<size_t>(d)];
            if (classifyAscentUseful(
                    model.continuous,
                    down,
                    c0ref.viable,
                    c0ref.surviveBin,
                    c0ref.endX,
                    c.viable,
                    c.surviveBin,
                    c.endX
                )) {
                plan.ascentUseful = true;
                break;
            }
        }
    }

    // Wave: no delay improves surviveBin / endX → Pass (avoid phase-shift "assists").
    // Never discard a surviving delay when d0 actually died in sim.
    {
        bool const isWave = real && real->m_isDart;
        int bestBin = 0;
        float bestGain = 0.f;
        float const baseEnd = cands[0].endX;
        bool anySavesFromD0Death = false;
        for (int d = 0; d <= limit; ++d) {
            auto const& c = cands[static_cast<size_t>(d)];
            if (c.surviveBin > bestBin) bestBin = c.surviveBin;
            float const gain = c.endX - baseEnd;
            if (gain > bestGain) bestGain = gain;
            if (d > 0 && cands[0].raw.died && c.viable && !c.raw.died) {
                anySavesFromD0Death = true;
            }
        }
        if (!anySavesFromD0Death && !plan.idleSafer && shouldWaveFutileDelayPass(
                isWave, orbIntent, bestBin, bestGain, plan.foresightOrbPad
            )) {
            this->m_simAbsTick = -1;
            restoreCapture(layer);
            this->m_simulating = false;
            this->parkGhosts();
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
                plan.planId, AssistVerdict::name(plan.reason), bestBin, bestGain, plan.budgetHit
            ));
            publishLedger(0, true, DevRejectLedger::EarlyExit::WaveFutile);
            return plan;
        }
    }

    for (auto& c : cands) {
        c.orbUseful = false;
        if (!orbAssist || !c.activatedRing || !c.viable) continue;
        // Viable activation is useful for ranking — do not require spatialHint
        // when d0 already "safe" but missed the ring (orb-miss G1).
        if (!cands[0].clearsThreat) {
            c.orbUseful = true;
            continue;
        }
        if (!cands[0].activatedRing) {
            c.orbUseful = true;
            continue;
        }
        c.orbUseful = true;
    }

    auto pickRobust = [&](std::vector<int> const& pool, bool needActivation, bool preferOutcome) -> int {
        if (pool.empty()) return -1;
        int const k = robustK;
        int best = -1;
        Cand bestC{};
        bool have = false;
        for (int d : pool) {
            if (d < 0 || d > limit) continue;
            auto const& c = cands[static_cast<size_t>(d)];
            if (!c.viable) continue;
            if (needActivation && !c.activatedRing) continue;
            bool ok = true;
            for (int e = d - k; e <= d + k; ++e) {
                if (e < 0 || e > limit) continue;
                auto const& n = cands[static_cast<size_t>(e)];
                if (!n.viable) {
                    ok = false;
                    break;
                }
                if (needActivation && !n.activatedRing) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            if (!have || betterCand(c, bestC, latencyWeight, preferOutcome)) {
                best = d;
                bestC = c;
                have = true;
            }
        }
        if (!have) {
            for (int d : pool) {
                if (d < 0 || d > limit) continue;
                auto const& c = cands[static_cast<size_t>(d)];
                if (!c.viable) continue;
                if (needActivation && !c.activatedRing) continue;
                if (!have || betterCand(c, bestC, latencyWeight, preferOutcome)) {
                    best = d;
                    bestC = c;
                    have = true;
                }
            }
        }
        return best;
    };

    // Optional: top cand must also clear threat under firstDt ±3%.
    auto jitterOk = [&](int d, int releaseAfter) -> bool {
        if (!options.robustJitter || d < 0) return true;
        if (tickBudget <= 0 || wallExceeded()) return true; // soft accept
        static float const kScales[] = { 0.97f, 1.03f };
        for (float s : kScales) {
            float jd = firstDt * s;
            if (jd <= 0.f || jd > dt * 1.5f) jd = firstDt;
            auto r = runOne(
                layer, real, down, d, limit, dt, jd, model, releaseAfter,
                zone.valid ? &zone : nullptr, &tickBudget
            );
            this->resetCaptureForNextRun(layer);
            if (!clearsThreatOf(r, zone) || r.diedBeforeAction) return false;
        }
        return true;
    };

    int actMin = -1, actMax = -1;
    for (int d = 0; d <= limit; ++d) {
        auto const& c = cands[static_cast<size_t>(d)];
        if (c.viable && c.activatedRing) {
            if (actMin < 0) actMin = d;
            actMax = d;
        }
    }
    // Window-cap: activation band never reported past limit.
    if (actMin > limit) actMin = limit;
    if (actMax > limit) actMax = limit;
    plan.dMin = actMin;
    plan.dMax = actMax;
    ledgerActMin = actMin;
    ledgerActMax = actMax;

    bool const legacyPick = options.legacyPick;

    // Legacy orb: earliest ±k-robust activation (first hit in band).
    auto pickEarliestRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
        if (pool.empty()) return -1;
        int const k = robustK;
        int best = -1;
        for (int d : pool) {
            if (d < 0 || d > limit) continue;
            auto const& c = cands[static_cast<size_t>(d)];
            if (!c.viable) continue;
            if (needActivation && !c.activatedRing) continue;
            bool ok = true;
            for (int e = d - k; e <= d + k; ++e) {
                if (e < 0 || e > limit) continue;
                auto const& n = cands[static_cast<size_t>(e)];
                if (!n.viable) {
                    ok = false;
                    break;
                }
                if (needActivation && !n.activatedRing) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            if (best < 0 || d < best) best = d;
        }
        if (best >= 0) return best;
        for (int d : pool) {
            if (d < 0 || d > limit) continue;
            auto const& c = cands[static_cast<size_t>(d)];
            if (!c.viable) continue;
            if (needActivation && !c.activatedRing) continue;
            return d;
        }
        return -1;
    };

    // Legacy/non-orb miss-assist: latest ±k-robust (then latest viable).
    // Earliest-viable jumped too soon after a short delay and still died.
    auto pickLatestRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
        if (pool.empty()) return -1;
        int const k = robustK;
        int best = -1;
        for (int d : pool) {
            if (d < 0 || d > limit) continue;
            auto const& c = cands[static_cast<size_t>(d)];
            if (!c.viable) continue;
            if (needActivation && !c.activatedRing) continue;
            bool ok = true;
            for (int e = d - k; e <= d + k; ++e) {
                if (e < 0 || e > limit) continue;
                auto const& n = cands[static_cast<size_t>(e)];
                if (!n.viable) {
                    ok = false;
                    break;
                }
                if (needActivation && !n.activatedRing) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            if (best < 0 || d > best) best = d;
        }
        if (best >= 0) return best;
        for (int i = static_cast<int>(pool.size()) - 1; i >= 0; --i) {
            int const d = pool[static_cast<size_t>(i)];
            if (d < 0 || d > limit) continue;
            auto const& c = cands[static_cast<size_t>(d)];
            if (!c.viable) continue;
            if (needActivation && !c.activatedRing) continue;
            return d;
        }
        return -1;
    };

    auto applyChosenMeta = [&](int pick) {
        if (pick < 0 || pick > limit) return;
        auto const& c = cands[static_cast<size_t>(pick)];
        plan.threatCleared = c.clearsThreat;
        plan.activatedRing = c.activatedRing;
        plan.requireHold = c.requireHold;
        plan.orbUseful = c.orbUseful;
    };

    int chosen = -1;
    bool doPass = false;
    bool haveActViable = actMin >= 0;

    if (orbIntent && haveActViable) {
        std::vector<int> pool;
        for (int d = actMin; d <= actMax; ++d) pool.push_back(d);
        int const k = robustK;
        auto tryPick = [&](std::vector<int> const& p) -> int {
            for (;;) {
                // Orb window: Legacy earliest-first; New outcome/exit rank ±k.
                int pick = legacyPick
                    ? pickEarliestRobust(p, true)
                    : pickRobust(p, true, true);
                if (pick < 0) return -1;
                int releaseAfter = cands[static_cast<size_t>(pick)].requireHold ? 40 : -1;
                if (jitterOk(pick, releaseAfter)) return pick;
                cands[static_cast<size_t>(pick)].viable = false;
                cands[static_cast<size_t>(pick)].robustKFailed = true;
            }
        };
        if (actMax - actMin >= 2 * k) {
            std::vector<int> interior;
            for (int d = actMin + k; d <= actMax - k; ++d) interior.push_back(d);
            chosen = tryPick(interior);
        }
        if (chosen < 0) chosen = tryPick(pool);
        if (chosen >= 0) {
            applyChosenMeta(chosen);
            plan.requireOrb = true;
        }
    }
    else if (legacyPick) {
        // Legacy: d0 viable → Pass. Else latest ±k-robust in window (miss-assist wait).
        if (cands[0].viable) {
            int releaseAfter = cands[0].requireHold ? 40 : -1;
            if (jitterOk(0, releaseAfter)) {
                doPass = true;
                chosen = 0;
                plan.threatCleared = true;
                plan.nowIsSafe = true;
            }
            else {
                cands[0].viable = false;
                cands[0].robustKFailed = true;
                std::vector<int> pool;
                for (int d = 0; d <= limit; ++d) pool.push_back(d);
                for (;;) {
                    chosen = pickLatestRobust(pool, false);
                    if (chosen < 0) break;
                    int releaseAfter2 = cands[static_cast<size_t>(chosen)].requireHold ? 40 : -1;
                    if (jitterOk(chosen, releaseAfter2)) break;
                    cands[static_cast<size_t>(chosen)].viable = false;
                    cands[static_cast<size_t>(chosen)].robustKFailed = true;
                    chosen = -1;
                }
                applyChosenMeta(chosen);
            }
        }
        else {
            std::vector<int> pool;
            for (int d = 0; d <= limit; ++d) pool.push_back(d);
            for (;;) {
                chosen = pickLatestRobust(pool, false);
                if (chosen < 0) break;
                int releaseAfter = cands[static_cast<size_t>(chosen)].requireHold ? 40 : -1;
                if (jitterOk(chosen, releaseAfter)) break;
                cands[static_cast<size_t>(chosen)].viable = false;
                cands[static_cast<size_t>(chosen)].robustKFailed = true;
                chosen = -1;
            }
            applyChosenMeta(chosen);
        }
    }
    else {
        // New (G1): d0 survived → Pass. Else best predicted exit among survivors (±k).
        if (cands[0].viable) {
            int releaseAfter = cands[0].requireHold ? 40 : -1;
            if (jitterOk(0, releaseAfter)) {
                doPass = true;
                chosen = 0;
                plan.threatCleared = true;
                plan.nowIsSafe = true;
            }
            else {
                cands[0].viable = false;
                cands[0].robustKFailed = true;
            }
        }
        if (!doPass) {
            std::vector<int> pool;
            for (int d = 0; d <= limit; ++d) pool.push_back(d);
            for (;;) {
                chosen = pickRobust(pool, false, true);
                if (chosen < 0) break;
                int releaseAfter = cands[static_cast<size_t>(chosen)].requireHold ? 40 : -1;
                if (jitterOk(chosen, releaseAfter)) break;
                cands[static_cast<size_t>(chosen)].viable = false;
                cands[static_cast<size_t>(chosen)].robustKFailed = true;
                chosen = -1;
            }
            if (chosen == 0) {
                doPass = true;
                plan.threatCleared = true;
                plan.nowIsSafe = cands[0].viable;
            }
            applyChosenMeta(chosen);
        }
    }

    // G1 window-cap assert.
    if (chosen > limit) chosen = limit;
    // Hard gate: never arm a Delay the sim marked as death.
    if (chosen > 0 && chosen <= limit) {
        auto const& ch = cands[static_cast<size_t>(chosen)];
        if (!ch.viable || ch.raw.died || ch.diedBeforeAction) {
            AssistLog::debug(fmt::format(
                "PLAN id={} reject lethal Delay +{} → Pass",
                plan.planId, chosen
            ));
            chosen = -1;
            doPass = false;
            plan.requireOrb = false;
        }
    }
    if (chosen < 0) {
        // Partial budget: if any activation survived, take earliest act ≤ limit.
        if (plan.budgetHit && actMin >= 0 && actMin <= limit
            && cands[static_cast<size_t>(actMin)].viable
            && !cands[static_cast<size_t>(actMin)].raw.died) {
            chosen = actMin;
            doPass = false;
            applyChosenMeta(chosen);
            plan.requireOrb = true;
            AssistLog::debug(fmt::format(
                "PLAN id={} budget-partial actMin={} pick={}",
                plan.planId, actMin, chosen
            ));
        }
    }

    // Continuous miss-assist: idle safer + ascent helps → Delay, not Pass/hopeless.
    // Only when a later *surviving* cand exists in-window.
    if (plan.idleSafer && plan.ascentUseful
        && (doPass || chosen <= 0)) {
        std::vector<int> pool;
        for (int d = 1; d <= limit; ++d) pool.push_back(d);
        int forced = legacyPick
            ? pickLatestRobust(pool, false)
            : pickRobust(pool, false, true);
        if (forced > 0
            && cands[static_cast<size_t>(forced)].viable
            && !cands[static_cast<size_t>(forced)].raw.died) {
            chosen = forced;
            doPass = false;
            applyChosenMeta(chosen);
            AssistLog::debug(fmt::format(
                "PLAN id={} idleSafer+ascent → Delay +{} (was Pass/hopeless)",
                plan.planId, chosen
            ));
        }
    }
    // Re-assert lethal gate after idleSafer / budget-partial.
    if (chosen > 0 && chosen <= limit) {
        auto const& ch = cands[static_cast<size_t>(chosen)];
        if (!ch.viable || ch.raw.died || ch.diedBeforeAction) {
            chosen = -1;
            doPass = false;
            plan.requireOrb = false;
        }
    }

    // G0 A/B: on disagreement with earliest-viable (legacy), fair coin + log arm.
    if (StageConfig::assistVerifyAb()) {
        int legacy = -1;
        if (cands[0].viable) legacy = 0;
        else {
            for (int d = 1; d <= limit; ++d) {
                if (cands[static_cast<size_t>(d)].viable) {
                    legacy = d;
                    break;
                }
            }
        }
        int const neu = (doPass || chosen == 0) ? 0 : chosen;
        if (legacy >= 0 && legacy != neu) {
            bool const useLegacy = (std::rand() & 1) != 0;
            AssistLog::debug(fmt::format(
                "VERIFY id={} arm={} legacy={} new={} picked={}",
                plan.planId, useLegacy ? "legacy" : "new", legacy, neu,
                useLegacy ? legacy : neu
            ));
            if (useLegacy) {
                if (neu >= 0 && neu <= limit && neu != legacy) {
                    cands[static_cast<size_t>(neu)].verifyFailed = true;
                }
                chosen = legacy;
                doPass = (legacy == 0);
                plan.requireOrb = false;
                plan.requireHold = false;
                if (legacy > 0 && legacy <= limit) {
                    auto const& c = cands[static_cast<size_t>(legacy)];
                    plan.threatCleared = c.clearsThreat;
                    plan.activatedRing = c.activatedRing;
                    plan.requireHold = c.requireHold;
                }
            }
            else {
                if (legacy >= 0 && legacy <= limit && legacy != neu) {
                    cands[static_cast<size_t>(legacy)].verifyFailed = true;
                }
                AssistLog::debug(fmt::format(
                    "VERIFY id={} keeping new={}", plan.planId, neu
                ));
            }
        }
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

        for (int rel : HOLD_PROBE_RELEASES) {
            if (tried >= HOLD_PROBE_MAX) break;
            if (tickBudget <= 0 || wallExceeded()) break;
            if (base.releaseAfter >= 0 && rel == base.releaseAfter) continue;
            ++tried;
            auto r = runOne(
                layer, real, down, chosen, limit, dt, firstDt, model, rel,
                zone.valid ? &zone : nullptr, &tickBudget
            );
            this->resetCaptureForNextRun(layer);
            ++plan.candidatesTried;

            // Hold branch must survive — same rule as Delay-viable.
            bool const ok = !r.died && !r.diedBeforeAction
                && (clearsThreatOf(r, zone) || !zone.valid || r.unknownTrunc);
            if (!ok) continue;

            int const sTicks = r.ticksBeforeNeutral > 0 ? r.ticksBeforeNeutral : r.ticksRan;
            int const sBin = surviveBinOf(sTicks);
            if (earliest < 0 || rel < earliest) earliest = rel;
            if (latest < 0 || rel > latest) latest = rel;
            if (bestRel < 0
                || sBin > bestBin
                || (sBin == bestBin && r.endX > bestEnd + 0.5f)
                || (sBin == bestBin && std::fabs(r.endX - bestEnd) <= 0.5f
                    && (model.continuous ? rel < bestRel : false))) {
                bestRel = rel;
                bestBin = sBin;
                bestEnd = r.endX;
            }
        }

        plan.holdScanTried = tried;
        if (earliest >= 0) {
            plan.releaseEarliest = earliest;
            plan.releaseLatest = latest >= earliest ? latest : earliest;
            if (bestRel < 0) {
                bestRel = pickPreferredRelease(
                    plan.releaseEarliest, plan.releaseLatest, model.continuous
                );
            }
            plan.releaseAfter = bestRel;
            if (plan.releaseAfter >= 40 || base.requireHold) {
                plan.requireHold = true;
            }
        }
        else if (base.releaseAfter >= 0) {
            plan.releaseAfter = base.releaseAfter;
            plan.releaseEarliest = base.releaseAfter;
            plan.releaseLatest = base.releaseAfter;
        }
    }

    this->m_simAbsTick = -1;
    restoreCapture(layer);
    this->m_simulating = false;
    this->parkGhosts();

    if (chosen < 0) {
        plan.action = PlanAction::Pass;
        plan.reason = plan.budgetHit
            ? AssistReason::PassBudget
            : AssistReason::PassHopeless;
        plan.delay = 0;
        AssistLog::debug(fmt::format(
            "PLAN id={} Pass verdict={} limit={} budgetHit={} orbIntent={}",
            plan.planId, AssistVerdict::name(plan.reason), limit, plan.budgetHit, orbIntent
        ));
        DevRejectLedger::EarlyExit early = DevRejectLedger::EarlyExit::None;
        if (plan.budgetHit) early = DevRejectLedger::EarlyExit::Budget;
        else if (ledgerPreferEarlyExit) early = DevRejectLedger::EarlyExit::PreferBand;
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
    }
    else {
        plan.action = PlanAction::Delay;
        plan.reason = AssistVerdict::inferDelayReason(
            plan.activatedRing
                || cands[static_cast<size_t>(chosen)].activatedRing,
            plan.requireOrb,
            hasPrefer
        );
        plan.delay = chosen;
        plan.jumpTick = cands[static_cast<size_t>(chosen)].jumpTick;
        plan.nowIsSafe = false;
        plan.activatedRing = plan.activatedRing
            || cands[static_cast<size_t>(chosen)].activatedRing;
        if (plan.releaseAfter < 0 && cands[static_cast<size_t>(chosen)].releaseAfter >= 0) {
            plan.releaseAfter = cands[static_cast<size_t>(chosen)].releaseAfter;
            plan.releaseEarliest = plan.releaseAfter;
            plan.releaseLatest = plan.releaseAfter;
        }
        plan.requireHold = plan.requireHold
            || cands[static_cast<size_t>(chosen)].requireHold;
        // Non-orb: publish survivor band for planSequence exit prediction.
        if (!plan.requireOrb) {
            int sLo = -1;
            int sHi = -1;
            for (int d = 0; d <= limit; ++d) {
                auto const& c = cands[static_cast<size_t>(d)];
                if (!c.viable || c.raw.died) continue;
                if (sLo < 0) sLo = d;
                sHi = d;
            }
            if (sLo >= 0) {
                plan.dMin = sLo;
                plan.dMax = sHi;
            }
        }
    }

    // Dash orbs: sim keeps Jump down via tapRelease exceptions, but live inject
    // used to default releaseAfter=1 and cancel the dash. Force hold when the
    // winning cand dashed.
    if (chosen >= 0 && chosen <= limit) {
        auto const& win = cands[static_cast<size_t>(chosen)];
        if (win.raw.wasDashing) {
            plan.requireHold = true;
            plan.activatedRing = true;
            if (plan.releaseAfter < 40) plan.releaseAfter = 40;
            if (plan.releaseEarliest < 0 || plan.releaseEarliest > plan.releaseAfter) {
                plan.releaseEarliest = plan.releaseAfter;
            }
            if (plan.releaseLatest < 120) plan.releaseLatest = 120;
        }
    }

    if (plan.action == PlanAction::Delay && plan.reason == AssistReason::None) {
        plan.reason = AssistVerdict::inferDelayReason(
            plan.activatedRing, plan.requireOrb, hasPrefer
        );
    }
    if (plan.action == PlanAction::Pass && plan.reason == AssistReason::None) {
        plan.reason = AssistReason::PassUnknown;
    }

    bool const orbIntentOut = plan.requireOrb || plan.activatedRing || plan.usedOrbWindow;
    AssistLog::debug(fmt::format(
        "PLAN id={} {} verdict={} delay={} orbIntent={} foresight={} idleSafer={} ascent={} ring={} useful={} hold={} rel=[{},{}]@{} scan={} dMin={} dMax={} tried={} budgetLeft={}",
        plan.planId,
        plan.action == PlanAction::Pass ? "Pass" : "Delay",
        AssistVerdict::name(plan.reason),
        plan.delay,
        orbIntentOut ? 1 : 0,
        plan.foresightOrbPad ? 1 : 0,
        plan.idleSafer ? 1 : 0,
        plan.ascentUseful ? 1 : 0,
        plan.activatedRing, plan.orbUseful, plan.requireHold,
        plan.releaseEarliest, plan.releaseLatest, plan.releaseAfter, plan.holdScanTried,
        plan.dMin, plan.dMax, plan.candidatesTried, tickBudget
    ));
    {
        DevRejectLedger::EarlyExit early = DevRejectLedger::EarlyExit::None;
        if (plan.budgetHit) early = DevRejectLedger::EarlyExit::Budget;
        else if (ledgerPreferEarlyExit) early = DevRejectLedger::EarlyExit::PreferBand;
        bool const pass = (plan.action == PlanAction::Pass);
        publishLedger(pass ? 0 : plan.delay, pass, early);
    }
    return plan;
}

SimPlan ShadowSimulator::planSequence(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    auto single = plan(layer, real, down, limit, dt, firstDt, options);
    int const depth = std::clamp(options.smartDepth, 1, 3);
    bool const newPick = !options.legacyPick;

    // Exit prediction: among candidate first delays, pick the one whose
    // horizon replay actually survives / reaches furthest (not mid/latest).
    std::vector<int> allowed;
    if (single.action == PlanAction::Pass) {
        allowed.push_back(0);
    }
    else {
        int lo = single.dMin >= 0 ? single.dMin : single.delay;
        int hi = single.dMax >= 0 ? single.dMax : single.delay;
        for (int d = std::max(0, lo); d <= std::min(limit, hi); ++d) {
            allowed.push_back(d);
        }
        if (allowed.empty()) allowed.push_back(single.delay);
    }
    bool const wantExitPredict = newPick && down && allowed.size() > 1;
    if ((!wantExitPredict && depth <= 1) || !down) {
        single.smartDepthUsed = 1;
        return single;
    }

    Model model = modelFor(real, limit, options.capMul);
    int horizon = options.smartHorizon > 0 ? options.smartHorizon : std::max(model.cap, 180);
    horizon = std::clamp(horizon, 32, 320);
    int comboBudget = std::clamp(options.smartCombos, 8, 256);

    int slot = captureSnapshot(real);
    if (slot < 0) return single;

    struct Best {
        bool found = false;
        int d0 = 0;
        int score = -1;
        bool survived = false;
    } best;

    auto pushTap = [&](std::vector<InputEvent>& tl, int pressTick, bool tap) {
        tl.push_back(InputEvent{ pressTick, true });
        if (tap) tl.push_back(InputEvent{ pressTick + 2, false });
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
        ? BakeMap::get().nextClicksAfter(
            real->getPositionX(), true, player2, kind, std::min(chainDepth, 2)
          )
        : std::vector<BakedClick const*>{};

    float const speed = std::max(0.5f, real->m_playerSpeed * 311.2f * std::max(dt, 1.f / 240.f));
    std::vector<BakedClick const*> chain;
    for (auto const* c : followUps) {
        if (!c) continue;
        if (c->mode != kind) break;
        float const dx = c->x - real->getPositionX();
        int const eta = static_cast<int>(std::lround(dx / speed));
        if (eta <= 0 || eta >= horizon) continue;
        chain.push_back(c);
        if (static_cast<int>(chain.size()) >= chainDepth) break;
    }
    bool const useBakeChain = !chain.empty();

    for (int d0 : allowed) {
        if (combos >= comboBudget) break;

        if (useBakeChain) {
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            for (auto const* c : chain) {
                float const dx = c->x - real->getPositionX();
                int tFollow = std::max(d0 + minGap, static_cast<int>(std::lround(dx / speed)));
                int const midFrame = (c->earliest + c->latest) / 2;
                int const approxNow = options.attemptStartTick;
                if (midFrame > approxNow) {
                    tFollow = std::max(d0 + minGap, midFrame - approxNow);
                }
                if (tFollow >= horizon) continue;
                pushTap(tl, tFollow, tap);
            }
            auto rr = replay(layer, slot, 0, tl, dt, horizon, false, 0);
            ++combos;
            bool const survived = !rr.died || rr.unknown;
            int sc = survived ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing) sc += 5000;
            if (!chain.empty() && survived) {
                float const targetX = chain.front()->x;
                if (rr.endX + BakeMap::LOOKUP_X_TOL >= targetX) sc += 20000;
                else sc -= 10000;
            }
            if (!best.found || sc > best.score) {
                best = { true, d0, sc, survived };
            }
            continue;
        }

        {
            // Single-press exit probe: which first delay actually clears ahead?
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            auto rr = replay(layer, slot, 0, tl, dt, horizon, false, 0);
            ++combos;
            bool const survived = !rr.died || rr.unknown;
            int sc = survived ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing) sc += 5000;
            if (!best.found || sc > best.score) {
                best = { true, d0, sc, survived };
            }
        }
        if (depth >= 2) {
            for (int d1 = d0 + minGap; d1 < horizon && combos < comboBudget; d1 += step) {
                std::vector<InputEvent> tl2;
                pushTap(tl2, d0, tap);
                pushTap(tl2, d1, tap);
                auto rr2 = replay(layer, slot, 0, tl2, dt, horizon, false, 0);
                ++combos;
                bool const survived2 = !rr2.died || rr2.unknown;
                int sc2 = survived2 ? 100000 : rr2.deathTick * 50;
                float const lat2 = std::max(0.f, options.latencyWeight);
                sc2 += static_cast<int>(rr2.endX - lat2 * static_cast<float>(d0));
                if (rr2.activatedRing) sc2 += 5000;
                if (!best.found || sc2 > best.score) {
                    best = { true, d0, sc2, survived2 };
                }
            }
        }
    }

    single.smartDepthUsed = std::max(depth, wantExitPredict ? 1 : depth);
    single.smartCombosUsed = combos;
    // Only accept a predicted exit that actually survived the horizon.
    if (!best.found || !best.survived) {
        AssistLog::debug(fmt::format(
            "planSequence id={} no surviving exit — keep plan delay={}",
            single.planId, single.delay
        ));
        return single;
    }
    if (std::find(allowed.begin(), allowed.end(), best.d0) == allowed.end()) return single;

    if (best.d0 == 0) {
        single.action = PlanAction::Pass;
        single.reason = AssistReason::PassD0Safe;
        single.delay = 0;
        single.nowIsSafe = true;
    }
    else {
        single.action = PlanAction::Delay;
        single.delay = best.d0;
        single.nowIsSafe = false;
        if (!AssistVerdict::isDelay(single.reason)) {
            single.reason = AssistVerdict::inferDelayReason(
                single.activatedRing, single.requireOrb, false
            );
        }
    }
    AssistLog::debug(fmt::format(
        "planSequence id={} depth={} d0={} verdict={} allowed={} combos={} bakeChain={} exitPredict={}",
        single.planId, depth, best.d0, AssistVerdict::name(single.reason),
        allowed.size(), combos, useBakeChain, wantExitPredict ? 1 : 0
    ));
    return single;
}

ShadowSimulator::ReplayResult ShadowSimulator::replay(
    PlayLayer* layer,
    int slot,
    int startTick,
    std::vector<InputEvent> const& timeline,
    float dt,
    int maxTicks,
    bool shiftActionBy,
    int actionShift
) {
    ReplayResult out;
    auto* snap = snapshotSlot(slot);
    if (!layer || !snap || !m_shadow || dt <= 0.f || maxTicks <= 0) return out;

    copyPlayerState(m_shadow, snap);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;

    auto* s = m_shadow;
    // Build a shifted timeline view: each event tick += actionShift if requested.
    auto effectiveTick = [&](InputEvent const& ev) {
        if (!shiftActionBy) return ev.tick;
        return ev.tick + actionShift;
    };

    size_t nextEv = 0;
    // Apply any events at or before startTick immediately.
    while (nextEv < timeline.size() && effectiveTick(timeline[nextEv]) <= startTick) {
        if (timeline[nextEv].down) s->pushButton(PlayerButton::Jump);
        else s->releaseButton(PlayerButton::Jump);
        ++nextEv;
    }

    this->m_simTickBase = std::max(0, startTick);
    this->m_simAbsTick = -1;
    m_simulating = true;
    beginCapture(layer);

    for (int t = 0; t < maxTicks; ++t) {
        int const absTick = startTick + t;
        this->m_simAbsTick = absTick;

        while (nextEv < timeline.size() && effectiveTick(timeline[nextEv]) == absTick) {
            if (timeline[nextEv].down) s->pushButton(PlayerButton::Jump);
            else s->releaseButton(PlayerButton::Jump);
            ++nextEv;
        }

        stepPhysics(layer, s, dt, dt);
        out.endX = s->getPositionX();
        if (m_dead) {
            out.died = true;
            out.deathTick = t;
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
    this->m_simAbsTick = -1;
    restoreCapture(layer);
    m_simulating = false;
    parkGhosts();
    return out;
}

ShadowSimulator::BakeRunResult ShadowSimulator::runBakeTimeline(
    PlayLayer* layer,
    int slot,
    std::vector<ReplayEvent> const& events,
    float dt,
    int untilFrame,
    int sampleFrame
) {
    BakeRunResult out;
    auto* snap = this->snapshotSlot(slot);
    if (!layer || !snap || !m_shadow || dt <= 0.f || untilFrame < 0) return out;

    copyPlayerState(m_shadow, snap);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_deathObject = nullptr;

    auto* s = m_shadow;
    size_t nextEv = 0;

    auto applyEv = [&](ReplayEvent const& ev) {
        auto btn = PlayerButton::Jump;
        if (ev.button == 2) btn = PlayerButton::Left;
        else if (ev.button == 3) btn = PlayerButton::Right;
        if (ev.down) s->pushButton(btn);
        else s->releaseButton(btn);
    };

    this->m_simTickBase = 0;
    this->m_simAbsTick = -1;
    m_simulating = true;
    beginCapture(layer);

    for (int frame = 0; frame <= untilFrame; ++frame) {
        this->m_simAbsTick = frame;
        while (nextEv < events.size() && events[nextEv].frame == frame) {
            applyEv(events[nextEv]);
            ++nextEv;
        }

        if (sampleFrame >= 0 && frame == sampleFrame) {
            out.xAtSample = s->getPositionX();
            out.modeAtSample = StageConfig::kindOf(s);
            out.sampled = true;
        }

        stepPhysics(layer, s, dt, dt);
        if (m_dead || m_unknown) {
            out.died = true;
            out.deathFrame = frame;
            break;
        }
    }

    this->m_simAbsTick = -1;
    restoreCapture(layer);
    m_simulating = false;
    parkGhosts();
    return out;
}

ShadowSimulator::PathSample ShadowSimulator::samplePath(
    PlayLayer* layer,
    int slot,
    std::vector<ReplayEvent> const& events,
    float dt,
    int untilFrame
) {
    PathSample out;
    auto* snap = this->snapshotSlot(slot);
    if (!layer || !snap || !m_shadow || dt <= 0.f || untilFrame < 0) return out;

    copyPlayerState(m_shadow, snap);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_deathObject = nullptr;

    auto* s = m_shadow;
    size_t nextEv = 0;
    out.points.reserve(static_cast<size_t>(untilFrame) + 2);

    auto applyEv = [&](ReplayEvent const& ev) {
        auto btn = PlayerButton::Jump;
        if (ev.button == 2) btn = PlayerButton::Left;
        else if (ev.button == 3) btn = PlayerButton::Right;
        if (ev.down) s->pushButton(btn);
        else s->releaseButton(btn);
    };

    this->m_simTickBase = 0;
    this->m_simAbsTick = -1;
    m_simulating = true;
    beginCapture(layer);

    out.points.push_back(s->getPosition());
    for (int frame = 0; frame <= untilFrame; ++frame) {
        this->m_simAbsTick = frame;
        while (nextEv < events.size() && events[nextEv].frame == frame) {
            applyEv(events[nextEv]);
            ++nextEv;
        }
        stepPhysics(layer, s, dt, dt);
        out.points.push_back(s->getPosition());
        if (m_dead || m_unknown) {
            out.died = true;
            out.deathFrame = frame;
            break;
        }
    }

    this->m_simAbsTick = -1;
    restoreCapture(layer);
    m_simulating = false;
    parkGhosts();
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
        if (ticks < 24) return 0;
        if (ticks < 48) return 1;
        if (ticks < 96) return 2;
        return 3;
    };
    auto clears = [](RunResult const& r, ThreatZone const& zone) {
        if (!zone.valid) return !r.died || r.unknownTrunc;
        if (!r.died) return r.endX > zone.endX || r.zoneCleared;
        return r.deathX > zone.endX;
    };

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
    expect(
        shouldWaveHazardProximityPass(true, true, true, false),
        "wave-prox-pass"
    );
    expect(
        !shouldWaveHazardProximityPass(true, true, false, false),
        "wave-prox-orb-target-blocks"
    );
    expect(
        !shouldWaveHazardProximityPass(true, true, true, true),
        "wave-prox-foresight-blocks"
    );
    expect(
        !shouldWaveHazardProximityPass(false, true, true, false),
        "wave-prox-not-cube"
    );
    expect(
        !shouldWaveHazardProximityPass(true, false, true, false),
        "wave-prox-no-hazard"
    );
    expect(
        shouldWaveFutileDelayPass(true, false, 0, 3.f, false),
        "wave-futile-pass"
    );
    expect(
        !shouldWaveFutileDelayPass(true, true, 0, 3.f, false),
        "wave-futile-orb-intent"
    );
    expect(
        !shouldWaveFutileDelayPass(true, false, 0, 3.f, true),
        "wave-futile-foresight"
    );
    expect(
        !shouldWaveFutileDelayPass(true, false, 1, 3.f, false),
        "wave-futile-bin"
    );
    expect(
        !shouldWaveFutileDelayPass(true, false, 0, 12.f, false),
        "wave-futile-gain"
    );

    // Continuous idleSafer / ascentUseful
    expect(
        classifyIdleSafer(true, true, false, -1, 0.f, true, 4, 100.f),
        "idle-safer-alive"
    );
    expect(
        !classifyIdleSafer(false, true, false, -1, 0.f, true, 4, 100.f),
        "idle-safer-not-cube"
    );
    expect(
        !classifyIdleSafer(true, true, false, -1, 0.f, false, -1, 0.f),
        "idle-safer-d0-safe"
    );
    expect(
        classifyIdleSafer(true, true, true, 10, 200.f, true, 3, 100.f),
        "idle-safer-later-death"
    );
    expect(
        deathLooksLikeAscent(100.f, 140.f, false),
        "ascent-death-above"
    );
    expect(
        !deathLooksLikeAscent(100.f, 105.f, false),
        "ascent-death-near"
    );
    expect(
        deathLooksLikeAscent(100.f, 60.f, true),
        "ascent-death-flipped"
    );
    expect(
        classifyAscentUseful(true, true, false, 0, 50.f, true, 0, 50.f),
        "ascent-later-viable"
    );
    expect(
        classifyAscentUseful(true, true, true, 0, 50.f, true, 2, 50.f),
        "ascent-better-bin"
    );
    expect(
        !classifyAscentUseful(true, true, true, 1, 50.f, true, 1, 52.f),
        "ascent-no-gain"
    );

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
    struct Snap { float deathX; int clearDelay; };
    Snap snaps[] = {
        { 150.f, 4 }, { 120.f, 3 }, { 190.f, 6 }, { 105.f, 2 }, { 175.f, 5 },
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

    if (fails == 0) report = "regressionSelfCheck OK (bins+zone+5 snaps)";
    else report = fmt::format("regressionSelfCheck {} fails: {}", fails, report);
    return fails == 0;
}

void ShadowSimulator::applyPlayerBlob(PlayerObject* dst, DevPlayerBlob const& blob) {
    if (!dst || !blob.present) return;
    dst->setPosition({ blob.x, blob.y });
    dst->m_position = cocos2d::CCPoint{ blob.x, blob.y };
    dst->setRotation(blob.rotation);
    dst->setScaleX(blob.scaleX);
    dst->setScaleY(blob.scaleY);
    dst->m_yVelocity = blob.yVelocity;
    dst->m_fallSpeed = blob.fallSpeed;
    dst->m_gravity = blob.gravity;
    dst->m_gravityMod = blob.gravityMod;
    dst->m_speedMultiplier = blob.speedMultiplier;
    dst->m_playerSpeed = blob.playerSpeed;
    dst->m_vehicleSize = blob.vehicleSize;
    dst->m_platformerXVelocity = blob.platformerXVelocity;
    dst->m_isUpsideDown = blob.isUpsideDown;
    dst->m_isSideways = blob.isSideways;
    dst->m_isGoingLeft = blob.isGoingLeft;
    dst->m_isShip = blob.isShip;
    dst->m_isBall = blob.isBall;
    dst->m_isBird = blob.isBird;
    dst->m_isSwing = blob.isSwing;
    dst->m_isDart = blob.isDart;
    dst->m_isRobot = blob.isRobot;
    dst->m_isSpider = blob.isSpider;
    dst->m_isSecondPlayer = blob.isSecondPlayer;
    dst->m_isPlatformer = blob.isPlatformer;
    dst->m_isDashing = blob.isDashing;
    dst->m_isOnGround = blob.isOnGround;
    dst->m_isOnGround2 = blob.isOnGround2;
    dst->m_isOnSlope = blob.isOnSlope;
    dst->m_wasOnSlope = blob.wasOnSlope;
    dst->m_jumpBuffered = blob.jumpBuffered;
    dst->m_wasJumpBuffered = blob.wasJumpBuffered;
    dst->m_stateRingJump = blob.stateRingJump;
    dst->m_hasEverJumped = blob.hasEverJumped;
    dst->m_hasEverHitRing = blob.hasEverHitRing;
    dst->m_isLocked = blob.isLocked;
    dst->m_controlsDisabled = blob.controlsDisabled;
    dst->m_maybeIsFalling = blob.maybeIsFalling;
    dst->m_maybeIsBoosted = blob.maybeIsBoosted;
    dst->m_isDead = false;
    hideGhost(dst);
}

void ShadowSimulator::applyEvalWorldRestore(
    PlayLayer* layer,
    DevLiveWitness const& witness,
    DevEvalRestore& restore
) {
    if (!layer) return;

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
        bool const wantOrbs = DevLiveWitness::hasWorldDep(
            witness, DevWitnessCategory::Orbs
        );
        if (wantOrbs || !witness.orbIds.empty()) {
            layer->m_gameState.m_activatedObjectIDs.clear();
            auto* children = layer->m_objectLayer
                ? layer->m_objectLayer->getChildren()
                : nullptr;
            for (int uid : witness.orbIds) {
                if (uid == 0) continue;
                std::pair<int, int> const activatedKey{ uid, 0 };
                layer->m_gameState.m_activatedObjectIDs[activatedKey] = 1;
                if (!children) continue;
                for (unsigned i = 0; i < children->count(); ++i) {
                    auto* obj = typeinfo_cast<EnhancedGameObject*>(
                        children->objectAtIndex(i)
                    );
                    if (!obj || obj->m_uniqueID != uid) continue;
                    this->noteActivated(obj);
                    obj->m_activated = true;
                    obj->m_isActivated = true;
                    if (auto* ring = typeinfo_cast<RingObject*>(obj)) {
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
        int const tick = witness.triggerApplyTick >= 0
            ? witness.triggerApplyTick
            : witness.clock.effectTick;
        TriggerWorldStore::get().applyAtTick(layer, tick);
        DevEvalRestore::noteTriggersSpooled(
            restore,
            witness.triggerStoreKey,
            witness.triggerStoreVersion,
            tick
        );
    }

    // --- Movers: freeze-world poses, else digest-only ---
    if (witness.freezeWorld) {
        auto* children = layer->m_objectLayer
            ? layer->m_objectLayer->getChildren()
            : nullptr;
        if (children) {
            for (auto const& pose : witness.moverPoses) {
                GameObject* obj = nullptr;
                for (unsigned i = 0; i < children->count(); ++i) {
                    auto* candidate = typeinfo_cast<GameObject*>(
                        children->objectAtIndex(i)
                    );
                    if (!candidate) continue;
                    if (candidate->m_uniqueID != pose.uniqueId) continue;
                    obj = candidate;
                    break;
                }
                if (!obj) continue;

                bool already = false;
                for (auto const& st : this->m_savedTransforms) {
                    if (st.uniqueId == pose.uniqueId) {
                        already = true;
                        break;
                    }
                }
                if (!already) {
                    SavedTransform st;
                    st.uniqueId = obj->m_uniqueID;
                    st.obj = obj;
                    st.x = obj->getPositionX();
                    st.y = obj->getPositionY();
                    st.rotation = obj->getRotation();
                    st.scaleX = obj->getScaleX();
                    st.scaleY = obj->getScaleY();
                    st.nodeVisible = obj->isVisible();
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
                TriggerWorldStore::writePose(obj, tp);
            }
        }
        DevEvalRestore::noteMoversPosesWritten(
            restore,
            witness.moverPoses,
            witness.hasHazardDigest ? witness.hazardDigest : 0
        );
    }
    else if (witness.hasHazardDigest) {
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
        if (!s.obj) continue;
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

ShadowSimulator::EvalFromFreezeResult ShadowSimulator::evalDelayFromFreeze(
    PlayLayer* layer,
    DevLiveWitness const& witness,
    int delay,
    int horizonTicks,
    bool down,
    int* tickBudgetLeft
) {
    EvalFromFreezeResult out;
    out.evalSimPathId = DevSimPathId::EvalDelayFromFreeze;
    this->m_evalAbort = false;

    if (!witness.valid || !witness.clock.valid
        || !DevClockContract::get().clockOk(witness.clock)) {
        out.clockMismatch = true;
        return out;
    }
    if (!layer || !this->m_evalGhosts[0] || layer != this->m_hostLayer) {
        return out;
    }
    if (!witness.player.present) {
        return out;
    }
    if (delay < 0) delay = 0;
    if (horizonTicks < 0) horizonTicks = 0;

    auto& clock = DevClockContract::get();
    auto steps = clock.stepSchedule(witness.clock, delay, horizonTicks);
    out.stepScheduleHash = clock.hashSteps(steps);
    if (steps.empty()) return out;

    out.restore = DevEvalRestore::beginFromWitness(
        witness, DevSimPathId::EvalDelayFromFreeze
    );

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

    bool const tapRelease = !ghost->m_isDart && !ghost->m_isShip
        && !ghost->m_isSwing && !ghost->m_isRobot;
    bool acted = false;
    bool tapReleased = false;
    out.actionTick = delay;
    out.ran = true;

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft) return false;
        if (*tickBudgetLeft <= 0) return true;
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
        }
        else if (this->m_unknown) {
            out.ticksRan = 1;
            out.unknown = true;
            out.endX = ghost->getPositionX();
        }
    }

    int const n = static_cast<int>(steps.size());
    for (int t = 0; t < n && !out.died && !out.unknown; ++t) {
        if (this->m_evalAbort) {
            out.aborted = true;
            break;
        }
        if (spend()) {
            out.budgetHit = true;
            break;
        }
        float const stepDt = steps[static_cast<size_t>(t)];
        out.ticksRan = t + 1;
        this->m_orbOverlapTick = false;
        this->m_simAbsTick = this->m_simTickBase + t;
        this->m_triggerWorldAppliedThisTick = false;

        if (!acted && t == delay) {
            acted = true;
            if (down) {
                ghost->pushButton(PlayerButton::Jump);
                ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            }
            else {
                ghost->releaseButton(PlayerButton::Jump);
                ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        }
        else if (acted && down && tapRelease && !tapReleased && this->m_jumped) {
            bool const touchingOrb = this->m_orbOverlapTick
                || (ghost->m_touchingRings && ghost->m_touchingRings->count() > 0);
            bool const dashing = ghost->m_isDashing;
            bool const justHitOrb = this->m_activatedRing && t <= delay + 4;
            if (!touchingOrb && !dashing && !justHitOrb) {
                tapReleased = true;
                ghost->releaseButton(PlayerButton::Jump);
            }
        }

        stepPhysics(layer, ghost, stepDt, witness.clock.simDt);
        if (this->m_dead) {
            out.died = true;
            out.deathTick = t;
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
    ghost->setPosition({ 0.f, 105.f });
    ghost->m_position = cocos2d::CCPoint{ 0.f, 105.f };
    hideGhost(ghost);
    if (this->m_evalGhosts[1]) {
        hideGhost(this->m_evalGhosts[1]);
        this->m_evalGhosts[1]->setPosition({ 0.f, 105.f });
        this->m_evalGhosts[1]->m_position = cocos2d::CCPoint{ 0.f, 105.f };
    }
    return out;
}

ShadowSimulator::EvalFromFreezeResult ShadowSimulator::replayActualFire(
    PlayLayer* layer,
    DevLiveWitness const& witness,
    int actualFireTickAbs,
    int horizonTicks,
    bool down,
    int* tickBudgetLeft
) {
    EvalFromFreezeResult out;
    out.evalSimPathId = DevSimPathId::ReplayActualFire;
    this->m_evalAbort = false;

    if (!witness.valid || !witness.clock.valid
        || !DevClockContract::get().clockOk(witness.clock)) {
        out.clockMismatch = true;
        return out;
    }
    // Delay from effectTick to actual fire (clamped ≥ 0).
    int delay = actualFireTickAbs - witness.clock.effectTick;
    if (delay < 0) delay = 0;

    // Reuse the schedule path but retag evalSimPathId / restore path id.
    out = this->evalDelayFromFreeze(
        layer, witness, delay, horizonTicks, down, tickBudgetLeft
    );
    out.evalSimPathId = DevSimPathId::ReplayActualFire;
    if (out.restore.valid) {
        out.restore.evalSimPathId = DevSimPathId::ReplayActualFire;
    }
    return out;
}


