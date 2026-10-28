#include "ShadowSimulator.hpp"
#include "BakeMap.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace geode::prelude;


constexpr int BRANCH_RELEASES[] = { 2, 5, 10, 18 };


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
        layer->m_objectLayer->addChild(p);
        p->retain();
        hideGhost(p);
        return p;
    };

    m_shadow = makeHidden("earlybuffer-shadow-player"_spr);
    for (int i = 0; i < SNAPSHOT_SLOTS; ++i) {
        auto const id = fmt::format("earlybuffer-snapshot-{}", i);
        m_snapshots[i] = makeHidden(id.c_str());
    }
    m_nextSnapshot = 0;
}

void ShadowSimulator::detach(bool layerDying) {
    // Never leave the sim flag set — quit can interrupt a traj rebuild mid-capture.
    m_simulating = false;
    m_applyingRing = false;
    m_orbScanning = false;
    m_orbOverlapTick = false;
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_deathObject = nullptr;
    m_saved.clear();
    // gd::map: `= {}` is ambiguous on libstdc++/Android — use clear().
    m_savedActivatedIDs.clear();

    auto kill = [layerDying](PlayerObject*& p) {
        if (!p) return;
        // Mid-level reattach: remove from tree. On quit: PlayerObject::removeFromParent
        // during teardown is a common crash source — drop our retain and let
        // PlayLayer::onQuit destroy the remaining child retain.
        if (!layerDying) {
            if (p->getParent()) p->removeFromParent();
        }
        else {
            // Hide so a half-frame of quit doesn't flash ghosts
            p->setVisible(false);
        }
        p->release();
        p = nullptr;
    };
    kill(m_shadow);
    for (auto& s : m_snapshots) kill(s);
}

bool ShadowSimulator::isOwnedGhost(PlayerObject* p) const {
    if (!p) return false;
    if (p == m_shadow) return true;
    for (auto* s : m_snapshots) {
        if (p == s) return true;
    }
    return false;
}

void ShadowSimulator::parkGhosts() {
    // Only the active sim body — snapshots must keep captured state for
    // bake / outcome verify / trajectory samplePath.
    if (!this->m_shadow) return;

    // Sim checkCollisions inserts the shadow into section grids. Leave those
    // slots empty so live collision / later sortSectionVector cannot touch us.
    if (auto* layer = PlayLayer::get()) {
        layer->removeObjectFromSection(this->m_shadow);
    }

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
    this->m_saved.clear();
    this->m_savedActivatedIDs.clear();

    auto evac = [&](PlayerObject* p) {
        if (!p) return;
        if (layer) {
            layer->removeObjectFromSection(p);
        }
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
            clearCollisionLogs(p);
        }
        hideGhost(p);
    };

    evac(this->m_shadow);
    for (auto* slot : this->m_snapshots) {
        evac(slot);
    }
}

void ShadowSimulator::markDead(PlayerObject* p, GameObject* object) {
    // Only the active sim body drives m_dead; snapshots are frozen copies.
    if (p == m_shadow) {
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
    if (!shadow || !ring || shadow != m_shadow) return;

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

void ShadowSimulator::beginCapture(PlayLayer* layer) {
    m_saved.clear();
    m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;
}

void ShadowSimulator::restoreCapture(PlayLayer* layer) {
    for (auto const& s : m_saved) {
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
    m_saved.clear();
    layer->m_gameState.m_activatedObjectIDs = m_savedActivatedIDs;
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

void ShadowSimulator::stepPreCollide(PlayLayer* layer, PlayerObject* s, float dt) {
    // Always use the full step dt here — tiny CBF remainders launch slopes
    // and also under-detect orb/spike overlaps.
    hideGhost(s);
    clearCollisionLogs(s);
    layer->checkCollisions(s, dt, false);
    hideGhost(s);
}

void ShadowSimulator::stepPhysics(PlayLayer* layer, PlayerObject* s, float stepDt, float dt) {
    hideGhost(s);
    s->update(stepDt);
    hideGhost(s);
    if (m_dead) return;
    clearCollisionLogs(s);
    float colDt = stepDt;
    if (colDt < dt) colDt = dt; // same rule as pre-collide: never a tiny remainder
    layer->checkCollisions(s, colDt, false);
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

    bool grew = true;
    int guard = 0;
    while (grew && guard++ < 32) {
        grew = false;
        for (unsigned i = 0; i < children->count(); ++i) {
            auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
            if (!isHazardLike(obj)) continue;
            float const x = obj->getPositionX();
            float half = obj->getContentSize().width * std::fabs(obj->getScaleX()) * 0.5f;
            if (half < 10.f) half = 10.f;
            float const oLo = x - half;
            float const oHi = x + half;
            if (oHi + kClusterGapX < z.startX || oLo - kClusterGapX > z.endX) continue;
            if (oLo < z.startX) {
                z.startX = oLo;
                grew = true;
            }
            if (oHi > z.endX) {
                z.endX = oHi;
                grew = true;
            }
        }
    }
    return z;
}

bool ShadowSimulator::spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit, float dt) {
    if (!layer || !real || !layer->m_objectLayer || dt <= 0.f) return false;
    float const aheadX = std::clamp(static_cast<float>(limit + 8) * 5.5f, 60.f, 220.f);
    float const px = real->getPositionX();
    float const py = real->getPositionY();
    float const yReach = real->m_isShip || real->m_isDart || real->m_isSwing ? 120.f : 90.f;

    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return false;
    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!obj) continue;
        switch (obj->m_objectType) {
            case GameObjectType::YellowJumpRing:
            case GameObjectType::PinkJumpRing:
            case GameObjectType::RedJumpRing:
            case GameObjectType::GravityRing:
            case GameObjectType::GreenRing:
            case GameObjectType::DropRing:
            case GameObjectType::DashRing:
            case GameObjectType::GravityDashRing:
            case GameObjectType::SpiderOrb:
            case GameObjectType::CustomRing:
                break;
            default:
                continue;
        }
        if (obj->m_isActivated || (typeinfo_cast<RingObject*>(obj) && static_cast<RingObject*>(obj)->m_claimTouch)) {
            continue;
        }
        float const ox = obj->getPositionX();
        float const oy = obj->getPositionY();
        if (ox < px - 10.f || ox > px + aheadX) continue;
        if (std::fabs(oy - py) > yReach) continue;
        return true;
    }
    return false;
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
    int const maxTicks = delay + std::max(1, model.cap);

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

    for (int t = 0; t < maxTicks; ++t) {
        if (spend()) break;
        float const stepDt = (t == 0) ? firstDt : dt;
        r.ticksRan = t + 1;
        m_orbOverlapTick = false;

        if (t < delay) {
            stepPreCollide(layer, s, dt);
            if (m_dead) {
                r.died = true;
                r.deathTick = t;
                r.deathX = s->getPositionX();
                r.deathObject = m_deathObject;
                r.diedBeforeAction = true;
                return finish(r);
            }
            if (m_unknown) {
                r.unknown = true;
                r.unknownTrunc = true;
                r.unknownTick = t;
                return finish(r);
            }
        }

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
    int scanTicks, float dt, float firstDt, int* tickBudgetLeft
) {
    OrbWindow win;
    if (!layer || !real || !m_shadow || scanTicks <= 0) return win;

    copyPlayerState(m_shadow, real);
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
    m_activatedRing = false;
    m_ringCount = 0;
    m_orbOverlapTick = false;
    m_deathObject = nullptr;

    auto* s = m_shadow;
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    m_orbScanning = true;
    for (int t = 0; t < scanTicks; ++t) {
        if (tickBudgetLeft) {
            if (*tickBudgetLeft <= 0) break;
            --(*tickBudgetLeft);
        }
        float const stepDt = (t == 0) ? firstDt : dt;
        m_orbOverlapTick = false;

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
    bool clearsThreat = false;
    bool unknownTrunc = false;
    bool activatedRing = false;
    bool orbUseful = false;
    bool requireHold = false;
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

static bool betterCand(Cand const& a, Cand const& b, float latencyWeight) {
    if (a.viable != b.viable) return a.viable && !b.viable;
    if (a.orbUseful != b.orbUseful) return a.orbUseful && !b.orbUseful;
    if (a.surviveBin != b.surviveBin) return a.surviveBin > b.surviveBin;
    float const scoreA = a.endX - latencyWeight * static_cast<float>(a.delay);
    float const scoreB = b.endX - latencyWeight * static_cast<float>(b.delay);
    if (scoreA != scoreB) return scoreA > scoreB;
    return a.delay < b.delay;
}

// 0 = auto from mode / speed / relative dt variance; else clamp 1–3.
static int resolveRobustK(PlayerObject* real, PlanOptions const& options) {
    int forced = options.robustK;
    if (forced <= 0) forced = StageConfig::robustK();
    if (forced > 0) return std::clamp(forced, 1, 3);

    int k = ShadowSimulator::kWindowMarginK; // classic default 1
    if (real) {
        if (real->m_isDart || real->m_isShip || real->m_isSwing) k = 2;
        if (real->m_playerSpeed > 1.1f) k = std::max(k, 2);
        if (real->m_playerSpeed > 1.5f) k = 3;
    }
    if (options.dtVariance > 0.08f) k = std::min(3, k + 1);
    return k;
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
        if (auto* gm = GameManager::sharedState()) {
            if (gm->getGameVariable("0010")) return false;
        }
        return real == layer->m_player1 || real == layer->m_player2;
    };

    m_simulating = true;
    beginCapture(layer);

    ThreatZone zone{};
    {
        // Probe d0 to build the same threat zone Live uses.
        auto c0 = runOne(
            layer, real, down, 0, limit, dt, firstDt, model, -1,
            nullptr, &tickBudget
        );
        restoreCapture(layer);
        beginCapture(layer);
        if (c0.died) {
            zone = buildThreatZone(layer, c0.deathX, c0.deathObject);
            out.threatZone = zone;
        }
        if (tickBudget <= 0 || wallExceeded()) {
            out.budgetHit = true;
            restoreCapture(layer);
            m_simulating = false;
            parkGhosts();
            return out;
        }
    }

    auto r = runOne(
        layer, real, down, delay, limit, dt, firstDt, model, -1,
        zone.valid ? &zone : nullptr, &tickBudget
    );
    restoreCapture(layer);
    beginCapture(layer);

    out.diedBeforeAction = r.diedBeforeAction;
    out.clearsThreat = clearsThreatOf(r, zone);
    out.unknown = r.unknown || r.unknownTrunc;
    bool viable = false;
    if (r.unknownTrunc) {
        if (!zone.valid) viable = true;
        else if (r.died) viable = r.deathX > zone.endX;
        else viable = r.endX > zone.endX || out.clearsThreat;
    }
    else {
        viable = out.clearsThreat && !r.diedBeforeAction;
    }

    if (viable && linkedDual()) {
        PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
        if (other && tickBudget > 0 && !wallExceeded()) {
            auto r2 = runOne(
                layer, other, down, delay, limit, dt, firstDt, model, -1,
                zone.valid ? &zone : nullptr, &tickBudget
            );
            restoreCapture(layer);
            beginCapture(layer);
            if (!clearsThreatOf(r2, zone) || r2.diedBeforeAction) {
                viable = false;
                out.clearsThreat = false;
            }
        }
    }

    if (tickBudget <= 0 || wallExceeded()) out.budgetHit = true;

    restoreCapture(layer);
    m_simulating = false;
    parkGhosts();

    // Budget exhaustion → do not trust bake (caller falls through / Pass).
    out.ok = viable && !out.budgetHit;
    return out;
}

SimPlan ShadowSimulator::plan(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    static int s_planId = 1;
    SimPlan plan;
    plan.smartDepthUsed = 1;
    plan.planId = s_planId++;
    if (!layer || !real || !m_shadow || limit < 0 || dt <= 0.f) return plan;
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

    m_simulating = true;
    beginCapture(layer);

    {
        auto base = runBaseline(layer, real, down, dt, firstDt, model, &tickBudget);
        restoreCapture(layer);
        beginCapture(layer);
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
        int scanTicks = std::max(limit + model.slack + 4, limit + 1);
        orbHint = scanOrbWindow(layer, real, down, scanTicks, dt, firstDt, &tickBudget);
        restoreCapture(layer);
        beginCapture(layer);
        if (orbHint.found) plan.usedOrbWindow = true;
        spatialHint = spatialOrbHint(layer, real, limit, dt);
    }

    ThreatZone zone{};
    std::vector<Cand> cands(static_cast<size_t>(limit) + 1);

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
        restoreCapture(layer);
        beginCapture(layer);
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
        if (c.unknownTrunc) {
            if (!zone.valid) c.viable = true;
            else if (c.raw.died) c.viable = c.deathX > zone.endX;
            else c.viable = c.raw.endX > zone.endX || c.clearsThreat;
        }
        else {
            c.viable = c.clearsThreat && !c.diedBeforeAction;
        }
        // Linked dual: other player's shadow must also clear the same zone.
        if (c.viable && linkedDual()) {
            PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
            if (other && tickBudget > 0) {
                auto r2 = runOne(
                    layer, other, down, c.delay, limit, dt, firstDt, model, releaseAfter,
                    zone.valid ? &zone : nullptr, &tickBudget
                );
                restoreCapture(layer);
                beginCapture(layer);
                if (!clearsThreatOf(r2, zone) || r2.diedBeforeAction) {
                    c.viable = false;
                    c.clearsThreat = false;
                }
            }
        }
    };

    {
        auto c0 = evalDelay(0, -1);
        plan.firstDeathTick = c0.raw.died ? c0.raw.deathTick : -1;
        plan.firstDeathObject = c0.raw.deathObject;
        if (c0.raw.died) {
            zone = buildThreatZone(layer, c0.raw.deathX, c0.raw.deathObject);
            plan.threatZone = zone;
        }
        finalizeViable(c0, -1);
        cands[0] = c0;

        // No threat target: baseline survives and no orb/pad ahead → Pass now.
        // Avoids buffering wave spam / empty corridors with nothing to forgive.
        bool const noThreat = !c0.raw.died && !zone.valid;
        bool const noOrbTarget = !(orbAssist && (orbHint.found || spatialHint || c0.activatedRing));
        if (noThreat && noOrbTarget) {
            restoreCapture(layer);
            m_simulating = false;
            parkGhosts();
            plan.action = PlanAction::Pass;
            plan.delay = 0;
            plan.nowIsSafe = true;
            plan.threatCleared = true;
            plan.jumpTick = c0.jumpTick;
            plan.activatedRing = c0.activatedRing;
            AssistLog::debug(fmt::format(
                "PLAN id={} Pass no-threat limit={} budgetHit={}",
                plan.planId, limit, plan.budgetHit
            ));
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
        if (tickBudget <= 0 || wallExceeded()) {
            plan.budgetHit = true;
            break;
        }
        auto c = evalDelay(d, -1);
        if (c.diedBeforeAction) break;

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
                restoreCapture(layer);
                beginCapture(layer);
                if (!br.died || br.unknownTrunc) {
                    c.raw = br;
                    c.activatedRing = br.activatedRing;
                    c.unknownTrunc = br.unknownTrunc;
                    c.endX = br.endX;
                    c.deathX = br.deathX;
                    c.surviveTicks = br.ticksBeforeNeutral > 0 ? br.ticksBeforeNeutral : br.ticksRan;
                    c.surviveBin = surviveBinOf(c.surviveTicks);
                    break;
                }
            }
        }
        finalizeViable(c, -1);
        cands[static_cast<size_t>(d)] = c;

        // Early-exit once prefer±k margin is evaluated and a robust prefer delay exists.
        if (hasPrefer && !plan.budgetHit && d == preferLastInOrder && preferBandRobust()) {
            AssistLog::debug(fmt::format(
                "PLAN id={} prefer-band early-exit [{},{}] after {} cands",
                plan.planId, preferLo, preferHi, plan.candidatesTried
            ));
            break;
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
        restoreCapture(layer);
        beginCapture(layer);
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
        }
    }

    bool anyActivation = false;
    for (auto const& c : cands) {
        if (c.activatedRing) anyActivation = true;
    }
    bool orbIntent = orbAssist && (orbHint.found || spatialHint || anyActivation);

    for (auto& c : cands) {
        c.orbUseful = false;
        if (!orbAssist || !c.activatedRing || !c.viable) continue;
        if (!cands[0].clearsThreat) {
            c.orbUseful = true;
            continue;
        }
        if (cands[0].viable && !cands[0].activatedRing && (orbHint.found || spatialHint)) {
            c.orbUseful = true;
            continue;
        }
        if (cands[0].viable && !cands[0].activatedRing) {
            c.orbUseful = false;
            continue;
        }
        c.orbUseful = true;
    }

    auto pickRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
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
            if (!have || betterCand(c, bestC, latencyWeight)) {
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
                if (!have || betterCand(c, bestC, latencyWeight)) {
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
            restoreCapture(layer);
            beginCapture(layer);
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
    plan.dMin = actMin;
    plan.dMax = actMax;

    int chosen = -1;
    bool doPass = false;
    bool haveActViable = actMin >= 0;

    if (orbIntent && haveActViable) {
        std::vector<int> pool;
        for (int d = actMin; d <= actMax; ++d) pool.push_back(d);
        int const k = robustK;
        auto tryPick = [&](std::vector<int> const& p) -> int {
            for (;;) {
                int pick = pickRobust(p, true);
                if (pick < 0) return -1;
                int releaseAfter = cands[static_cast<size_t>(pick)].requireHold ? 40 : -1;
                if (jitterOk(pick, releaseAfter)) return pick;
                cands[static_cast<size_t>(pick)].viable = false;
            }
        };
        if (actMax - actMin >= 2 * k) {
            std::vector<int> interior;
            for (int d = actMin + k; d <= actMax - k; ++d) interior.push_back(d);
            chosen = tryPick(interior);
        }
        if (chosen < 0) chosen = tryPick(pool);
        // No midpoint: earliest ±k-robust, else earliest viable (pickRobust fallback).
        if (chosen >= 0) {
            auto const& c = cands[static_cast<size_t>(chosen)];
            plan.orbUseful = c.orbUseful;
            plan.requireOrb = true;
            plan.requireHold = c.requireHold;
            plan.activatedRing = c.activatedRing;
            plan.threatCleared = c.clearsThreat;
        }
    }
    else if (cands[0].viable) {
        int releaseAfter = cands[0].requireHold ? 40 : -1;
        if (jitterOk(0, releaseAfter)) {
            doPass = true;
            chosen = 0;
            plan.threatCleared = true;
            plan.nowIsSafe = true;
        }
        else {
            cands[0].viable = false;
            std::vector<int> pool;
            for (int d = 0; d <= limit; ++d) pool.push_back(d);
            for (;;) {
                chosen = pickRobust(pool, false);
                if (chosen < 0) break;
                int releaseAfter2 = cands[static_cast<size_t>(chosen)].requireHold ? 40 : -1;
                if (jitterOk(chosen, releaseAfter2)) break;
                cands[static_cast<size_t>(chosen)].viable = false;
                chosen = -1;
            }
            if (chosen >= 0) {
                auto const& c = cands[static_cast<size_t>(chosen)];
                plan.threatCleared = c.clearsThreat;
                plan.activatedRing = c.activatedRing;
                plan.requireHold = c.requireHold;
                plan.orbUseful = c.orbUseful;
            }
        }
    }
    else {
        std::vector<int> pool;
        for (int d = 0; d <= limit; ++d) pool.push_back(d);
        for (;;) {
            chosen = pickRobust(pool, false);
            if (chosen < 0) break;
            int releaseAfter = cands[static_cast<size_t>(chosen)].requireHold ? 40 : -1;
            if (jitterOk(chosen, releaseAfter)) break;
            cands[static_cast<size_t>(chosen)].viable = false;
            chosen = -1;
        }
        if (chosen >= 0) {
            auto const& c = cands[static_cast<size_t>(chosen)];
            plan.threatCleared = c.clearsThreat;
            plan.activatedRing = c.activatedRing;
            plan.requireHold = c.requireHold;
            plan.orbUseful = c.orbUseful;
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
                AssistLog::debug(fmt::format(
                    "VERIFY id={} keeping new={}", plan.planId, neu
                ));
            }
        }
    }

    restoreCapture(layer);
    m_simulating = false;
    parkGhosts();

    if (chosen < 0) {
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        AssistLog::debug(fmt::format(
            "PLAN id={} Pass limit={} budgetHit={} orbIntent={}",
            plan.planId, limit, plan.budgetHit, orbIntent
        ));
        return plan;
    }

    if (doPass || chosen == 0) {
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.nowIsSafe = cands[0].viable;
        plan.jumpTick = cands[0].jumpTick;
        plan.activatedRing = cands[0].activatedRing;
    }
    else {
        plan.action = PlanAction::Delay;
        plan.delay = chosen;
        plan.jumpTick = cands[static_cast<size_t>(chosen)].jumpTick;
        plan.nowIsSafe = false;
    }

    AssistLog::debug(fmt::format(
        "PLAN id={} {} delay={} ring={} useful={} hold={} dMin={} dMax={} tried={} budgetLeft={}",
        plan.planId,
        plan.action == PlanAction::Pass ? "Pass" : "Delay",
        plan.delay, plan.activatedRing, plan.orbUseful, plan.requireHold,
        plan.dMin, plan.dMax, plan.candidatesTried, tickBudget
    ));
    return plan;
}

SimPlan ShadowSimulator::planSequence(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    auto single = plan(layer, real, down, limit, dt, firstDt, options);
    int const depth = std::clamp(options.smartDepth, 1, 3);
    if (depth <= 1 || !down) {
        single.smartDepthUsed = 1;
        return single;
    }

    Model model = modelFor(real, limit, options.capMul);
    int horizon = options.smartHorizon > 0 ? options.smartHorizon : std::max(model.cap, 180);
    horizon = std::clamp(horizon, 32, 320);
    int comboBudget = std::clamp(options.smartCombos, 8, 256);

    int slot = captureSnapshot(real);
    if (slot < 0) return single;

    std::vector<int> allowed;
    if (single.action == PlanAction::Pass) {
        allowed.push_back(0);
    }
    else {
        int lo = single.dMin >= 0 ? single.dMin : single.delay;
        int hi = single.dMax >= 0 ? single.dMax : single.delay;
        for (int d = std::max(0, lo); d <= std::min(limit, hi); ++d) allowed.push_back(d);
        if (allowed.empty()) allowed.push_back(single.delay);
    }

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
    auto followUps = BakeMap::get().loaded()
        ? BakeMap::get().nextClicksAfter(
            real->getPositionX(), true, player2, kind, std::min(depth - 1, 2)
          )
        : std::vector<BakedClick const*>{};

    // Drop follow-ups that change mode or lie beyond the smart horizon (by X).
    float const speed = std::max(0.5f, real->m_playerSpeed * 311.2f * std::max(dt, 1.f / 240.f));
    std::vector<BakedClick const*> chain;
    for (auto const* c : followUps) {
        if (!c) continue;
        if (c->mode != kind) break; // mode change aborts chain
        float const dx = c->x - real->getPositionX();
        int const eta = static_cast<int>(std::lround(dx / speed));
        if (eta <= 0 || eta >= horizon) continue;
        chain.push_back(c);
        if (static_cast<int>(chain.size()) >= depth - 1) break;
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
                // Prefer nominal frame center of bake window when convertible.
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
            // Reachability of next bake reference X.
            if (!chain.empty() && survived) {
                float const targetX = chain.front()->x;
                if (rr.endX + BakeMap::LOOKUP_X_TOL >= targetX) sc += 20000;
                else sc -= 10000; // missed next window
            }
            if (!best.found || sc > best.score) {
                best = { true, d0, sc, survived };
            }
            continue;
        }

        {
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            auto rr = replay(layer, slot, 0, tl, dt, horizon, false, 0);
            ++combos;
            int sc = (!rr.died || rr.unknown) ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing) sc += 5000;
            if (!best.found || sc > best.score) {
                best = { true, d0, sc, !rr.died || rr.unknown };
            }
        }
        if (depth >= 2) {
            for (int d1 = d0 + minGap; d1 < horizon && combos < comboBudget; d1 += step) {
                std::vector<InputEvent> tl2;
                pushTap(tl2, d0, tap);
                pushTap(tl2, d1, tap);
                auto rr2 = replay(layer, slot, 0, tl2, dt, horizon, false, 0);
                ++combos;
                int sc2 = (!rr2.died || rr2.unknown) ? 100000 : rr2.deathTick * 50;
                float const lat2 = std::max(0.f, options.latencyWeight);
                sc2 += static_cast<int>(rr2.endX - lat2 * static_cast<float>(d0));
                if (rr2.activatedRing) sc2 += 5000;
                if (!best.found || sc2 > best.score) {
                    best = { true, d0, sc2, !rr2.died || rr2.unknown };
                }
            }
        }
    }

    single.smartDepthUsed = depth;
    single.smartCombosUsed = combos;
    if (!best.found || !best.survived) return single;
    if (std::find(allowed.begin(), allowed.end(), best.d0) == allowed.end()) return single;

    if (best.d0 == 0) {
        single.action = PlanAction::Pass;
        single.delay = 0;
        single.nowIsSafe = true;
    }
    else {
        single.action = PlanAction::Delay;
        single.delay = best.d0;
        single.nowIsSafe = false;
    }
    AssistLog::debug(fmt::format(
        "planSequence id={} depth={} d0={} allowed={} combos={} bakeChain={}",
        single.planId, depth, best.d0, allowed.size(), combos, useBakeChain
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

    m_simulating = true;
    beginCapture(layer);

    for (int t = 0; t < maxTicks; ++t) {
        int const absTick = startTick + t;

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

    m_simulating = true;
    beginCapture(layer);

    for (int frame = 0; frame <= untilFrame; ++frame) {
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

    m_simulating = true;
    beginCapture(layer);

    out.points.push_back(s->getPosition());
    for (int frame = 0; frame <= untilFrame; ++frame) {
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


