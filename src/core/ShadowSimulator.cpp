#include "ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace frire {

namespace {
constexpr int BRANCH_RELEASES[] = { 2, 5, 10, 18 };
}

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

    m_shadow = makeHidden("frirecracker-shadow-player"_spr);
    for (int i = 0; i < SNAPSHOT_SLOTS; ++i) {
        auto const id = fmt::format("frirecracker-snapshot-{}", i);
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
    m_savedActivatedIDs = {};

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

void ShadowSimulator::markDead(PlayerObject* p, GameObject* object) {
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

#define FRIRE_COPY(field) dst->field = src->field

void ShadowSimulator::copyPlayerState(PlayerObject* dst, PlayerObject* src) {
    if (!dst || !src) return;

    dst->copyAttributes(src);
    dst->setPosition(src->getPosition());
    dst->setRotation(src->getRotation());
    dst->setScaleX(src->getScaleX());
    dst->setScaleY(src->getScaleY());
    hideGhost(dst);

    FRIRE_COPY(m_position);
    FRIRE_COPY(m_yVelocity);
    FRIRE_COPY(m_fallSpeed);
    FRIRE_COPY(m_gravity);
    FRIRE_COPY(m_gravityMod);
    FRIRE_COPY(m_speedMultiplier);
    FRIRE_COPY(m_yStart);
    FRIRE_COPY(m_playerSpeed);
    FRIRE_COPY(m_playerSpeedAC);
    FRIRE_COPY(m_vehicleSize);
    FRIRE_COPY(m_isUpsideDown);
    FRIRE_COPY(m_isSideways);
    FRIRE_COPY(m_isGoingLeft);
    FRIRE_COPY(m_isShip);
    FRIRE_COPY(m_isBall);
    FRIRE_COPY(m_isBird);
    FRIRE_COPY(m_isSwing);
    FRIRE_COPY(m_isDart);
    FRIRE_COPY(m_isRobot);
    FRIRE_COPY(m_isSpider);
    FRIRE_COPY(m_isSecondPlayer);
    FRIRE_COPY(m_isPlatformer);
    FRIRE_COPY(m_platformerXVelocity);
    FRIRE_COPY(m_platformerVelocityRelated);
    FRIRE_COPY(m_xVelocityRelated);
    FRIRE_COPY(m_xVelocityRelated2);
    FRIRE_COPY(m_yVelocityRelated);
    FRIRE_COPY(m_yVelocityRelated3);
    FRIRE_COPY(m_reverseRelated);
    FRIRE_COPY(m_maybeReverseSpeed);
    FRIRE_COPY(m_maybeReverseAcceleration);
    FRIRE_COPY(m_reverseSync);
    FRIRE_COPY(m_wasTeleported);
    FRIRE_COPY(m_fixGravityBug);
    FRIRE_COPY(m_shipRotation);
    FRIRE_COPY(m_accelerationOrSpeed);
    FRIRE_COPY(m_isAccelerating);
    FRIRE_COPY(m_maybeIsFalling);
    FRIRE_COPY(m_maybeIsBoosted);
    FRIRE_COPY(m_decreaseBoostSlide);
    FRIRE_COPY(m_physDeltaRelated);
    FRIRE_COPY(m_totalTime);
    FRIRE_COPY(m_gameModeChangedTime);
    FRIRE_COPY(m_lastMovedTime);

    FRIRE_COPY(m_isDashing);
    FRIRE_COPY(m_dashX);
    FRIRE_COPY(m_dashY);
    FRIRE_COPY(m_dashAngle);
    FRIRE_COPY(m_dashStartTime);
    FRIRE_COPY(m_dashRing);

    FRIRE_COPY(m_isOnGround);
    FRIRE_COPY(m_isOnGround2);
    FRIRE_COPY(m_isOnGround3);
    FRIRE_COPY(m_isOnGround4);
    FRIRE_COPY(m_lastGroundObject);
    FRIRE_COPY(m_maybeLastGroundObject);
    FRIRE_COPY(m_preLastGroundObject);
    FRIRE_COPY(m_lastGroundedPos);
    FRIRE_COPY(m_groundYVelocity);
    FRIRE_COPY(m_groundObjectMaterial);
    FRIRE_COPY(m_lastLandTime);
    FRIRE_COPY(m_lastJumpTime);
    FRIRE_COPY(m_lastFlipTime);
    FRIRE_COPY(m_lastSpiderFlipTime);
    FRIRE_COPY(m_collidedObject);
    FRIRE_COPY(m_collidingWithLeft);
    FRIRE_COPY(m_collidingWithRight);
    FRIRE_COPY(m_lastCollisionBottom);
    FRIRE_COPY(m_lastCollisionTop);
    FRIRE_COPY(m_lastCollisionLeft);
    FRIRE_COPY(m_lastCollisionRight);
    FRIRE_COPY(m_collidedTopMinY);
    FRIRE_COPY(m_collidedBottomMaxY);
    FRIRE_COPY(m_collidedLeftMaxX);
    FRIRE_COPY(m_collidedRightMinX);
    FRIRE_COPY(m_maybeIsColliding);
    FRIRE_COPY(m_objectSnappedTo);
    FRIRE_COPY(m_snapDistance);
    FRIRE_COPY(m_maybeTouchedBreakableBlock);
    FRIRE_COPY(m_isOnIce);
    FRIRE_COPY(m_isSliding);
    FRIRE_COPY(m_isSlidingRight);
    FRIRE_COPY(m_maybeSlidingTime);
    FRIRE_COPY(m_maybeSlidingStartTime);
    FRIRE_COPY(m_changedDirectionsTime);
    FRIRE_COPY(m_maybeChangedDirectionAngle);
    FRIRE_COPY(m_isMoving);
    FRIRE_COPY(m_maybeHasStopped);
    FRIRE_COPY(m_scaleXRelated);
    FRIRE_COPY(m_scaleXRelated2);
    FRIRE_COPY(m_scaleXRelated3);
    FRIRE_COPY(m_scaleXRelated4);
    FRIRE_COPY(m_scaleXRelated5);
    FRIRE_COPY(m_scaleXRelatedTime);

    FRIRE_COPY(m_isOnSlope);
    FRIRE_COPY(m_wasOnSlope);
    FRIRE_COPY(m_slopeVelocity);
    FRIRE_COPY(m_slopeAngle);
    FRIRE_COPY(m_slopeAngleRadians);
    FRIRE_COPY(m_slopeStartTime);
    FRIRE_COPY(m_slopeEndTime);
    FRIRE_COPY(m_slopeRotation);
    FRIRE_COPY(m_currentSlopeYVelocity);
    FRIRE_COPY(m_yVelocityBeforeSlope);
    FRIRE_COPY(m_currentSlope);
    FRIRE_COPY(m_currentSlope2);
    FRIRE_COPY(m_currentPotentialSlope);
    FRIRE_COPY(m_potentialSlopeMap);
    FRIRE_COPY(m_isCollidingWithSlope);
    FRIRE_COPY(m_collidingWithSlopeId);
    FRIRE_COPY(m_slopeFlipGravityRelated);
    FRIRE_COPY(m_slopeSlidingMaybeRotated);
    FRIRE_COPY(m_isCurrentSlopeTop);
    FRIRE_COPY(m_maybeUpsideDownSlope);
    FRIRE_COPY(m_maybeSlopeForce);
    FRIRE_COPY(m_maybeGoingCorrectSlopeDirection);

    FRIRE_COPY(m_wasJumpBuffered);
    FRIRE_COPY(m_wasRobotJump);
    FRIRE_COPY(m_fixRobotJump);
    FRIRE_COPY(m_stateJumpBuffered);
    FRIRE_COPY(m_stateRingJump);
    FRIRE_COPY(m_stateRingJump2);
    FRIRE_COPY(m_touchedRing);
    FRIRE_COPY(m_touchedCustomRing);
    FRIRE_COPY(m_touchedGravityPortal);
    FRIRE_COPY(m_touchedPad);
    FRIRE_COPY(m_padRingRelated);
    FRIRE_COPY(m_ringJumpRelated);
    FRIRE_COPY(m_ringRelatedSet);
    FRIRE_COPY(m_jumpPadRelated);
    FRIRE_COPY(m_blackOrbRelated);
    FRIRE_COPY(m_hasEverJumped);
    FRIRE_COPY(m_hasEverHitRing);
    FRIRE_COPY(m_lastPortalPos);
    FRIRE_COPY(m_lastActivatedPortal);
    FRIRE_COPY(m_touchedRings);

    FRIRE_COPY(m_stateOnGround);
    FRIRE_COPY(m_stateUnk);
    FRIRE_COPY(m_stateNoStickX);
    FRIRE_COPY(m_stateNoStickY);
    FRIRE_COPY(m_stateUnk2);
    FRIRE_COPY(m_stateBoostX);
    FRIRE_COPY(m_stateBoostY);
    FRIRE_COPY(m_maybeStateForce2);
    FRIRE_COPY(m_stateScale);
    FRIRE_COPY(m_stateNoAutoJump);
    FRIRE_COPY(m_stateDartSlide);
    FRIRE_COPY(m_stateHitHead);
    FRIRE_COPY(m_stateFlipGravity);
    FRIRE_COPY(m_stateForce);
    FRIRE_COPY(m_stateForceVector);
    FRIRE_COPY(m_affectedByForces);

    FRIRE_COPY(m_isLocked);
    FRIRE_COPY(m_controlsDisabled);
    FRIRE_COPY(m_inputsLocked);
    FRIRE_COPY(m_holdingRight);
    FRIRE_COPY(m_holdingLeft);
    FRIRE_COPY(m_leftPressedFirst);

    FRIRE_COPY(m_jumpBuffered);
    FRIRE_COPY(m_holdingButtons);
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

#undef FRIRE_COPY

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

ShadowSimulator::RunResult ShadowSimulator::runOne(
    PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
    float dt, float firstDt, Model const& model, int releaseAfter
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
    // Press: stay released until `delay`. Release: stay held until `delay`.
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    bool airborneSeen = false;
    int groundedStreak = 0;
    int const maxTicks = delay + std::max(1, model.cap);

    auto finish = [&](RunResult out) {
        out.activatedRing = m_activatedRing;
        out.ringCount = m_ringCount;
        out.endX = s->getPositionX();
        out.ticksRan = std::max(out.ticksRan, out.deathTick >= 0 ? out.deathTick + 1 : 0);
        return out;
    };

    for (int t = 0; t < maxTicks; ++t) {
        float const stepDt = (t == 0) ? firstDt : dt;
        r.ticksRan = t + 1;
        m_orbOverlapTick = false;

        // Waiting ticks only: collide before moving so "hold without pressing"
        // can die for real. NEVER do this on the action tick — that killed the
        // jump before pushButton and made every plan Pass (no assist at all).
        if (t < delay) {
            stepPreCollide(layer, s, dt);
            if (m_dead) {
                r.died = true;
                r.deathTick = t;
                r.deathObject = m_deathObject;
                r.diedBeforeAction = true;
                return finish(r);
            }
            if (m_unknown) {
                r.unknown = true;
                r.unknownTick = t;
                return finish(r);
            }
        }

        // Input (action tick) — must run before move+collide so a jump can save.
        if (!acted && t == delay) {
            acted = true;
            if (down) {
                s->pushButton(PlayerButton::Jump);
                // pushButton should set this; force it so orb collide this tick sees hold.
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
                // Dash orbs / still overlapping / just activated: keep holding.
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

        // Move, then hitboxes at the new position (real GD / CBF order).
        stepPhysics(layer, s, stepDt, dt);
        if (m_dead) {
            r.died = true;
            r.deathTick = t;
            r.deathObject = m_deathObject;
            r.diedBeforeAction = !acted;
            return finish(r);
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTick = t;
            return finish(r);
        }

        if (m_jumped && !r.jumped) {
            r.jumped = true;
            r.jumpTick = t;
        }

        if (down && !model.continuous && r.jumped) {
            bool const grounded = s->m_isOnGround || s->m_isOnGround2;
            if (!grounded) {
                airborneSeen = true;
                groundedStreak = 0;
            }
            else if (airborneSeen && ++groundedStreak >= 8) {
                return finish(r);
            }
        }
    }
    return finish(r);
}

ShadowSimulator::RunResult ShadowSimulator::runBaseline(
    PlayLayer* layer, PlayerObject* real, bool down,
    float dt, float firstDt, Model const& model
) {
    // Never apply the new input — keep the opposite of what the user just did.
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
        float const stepDt = (t == 0) ? firstDt : dt;
        stepPhysics(layer, s, stepDt, dt);
        r.ticksRan = t + 1;
        r.endX = s->getPositionX();
        if (m_dead) {
            r.died = true;
            r.deathTick = t;
            r.deathObject = m_deathObject;
            return r;
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTick = t;
            return r;
        }
    }
    return r;
}

ShadowSimulator::OrbWindow ShadowSimulator::scanOrbWindow(
    PlayLayer* layer, PlayerObject* real, bool down,
    int scanTicks, float dt, float firstDt
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
    // Don't apply the new press — fly as-is; scan mode records overlaps without claim.
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    m_orbScanning = true;
    for (int t = 0; t < scanTicks; ++t) {
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

namespace {

struct PlanCandidate {
    int delay = 0;
    bool ok = false;
    bool activatedRing = false;
    int ringCount = 0;
    int surviveTicks = 0;
    float endX = 0.f;
    int jumpTick = 0;
    bool unknown = false;
};

int candidateScore(PlanCandidate const& c, ShadowSimulator::OrbWindow const& orb, bool anyRingHit) {
    // Higher is better. Prefer: orb hit → in orb window → longer survival → farther x → earlier delay.
    int score = 0;
    if (!c.ok) return -1000000;
    if (c.activatedRing) score += 80000;
    score += c.ringCount * 3000;
    if (orb.found) {
        bool const inWin = c.delay >= orb.first && c.delay <= orb.last;
        bool const nearWin = c.delay >= std::max(0, orb.first - 1) && c.delay <= orb.last + 1;
        if (inWin) score += 20000;
        else if (nearWin) score += 5000;
        // Penalize pressing before the contact window without ever hitting the orb
        if (!c.activatedRing && c.delay < orb.first) score -= 25000;
    }
    // Jump-into-orb: scan may miss the window; still prefer any ring-activating delay
    if (anyRingHit && !c.activatedRing) score -= 40000;
    score += c.surviveTicks * 8;
    score += static_cast<int>(c.endX * 0.25f);
    score -= c.delay; // stable: earlier among equals
    return score;
}

}

SimPlan ShadowSimulator::plan(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    SimPlan plan;
    plan.smartDepthUsed = 1;
    if (!layer || !real || !m_shadow || limit < 0 || dt <= 0.f) return plan;
    if (firstDt <= 0.f || firstDt > dt) firstDt = dt;

    Model model = modelFor(real, limit, options.capMul);
    plan.continuous = model.continuous;

    auto const t0 = std::chrono::steady_clock::now();
    auto budgetExceeded = [&]() -> bool {
        if (options.budgetUs <= 0) return false;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        return us >= options.budgetUs;
    };

    m_simulating = true;
    beginCapture(layer);

    // Baseline: what happens if we never do this input.
    {
        auto base = runBaseline(layer, real, down, dt, firstDt, model);
        restoreCapture(layer);
        beginCapture(layer);
        if (base.died) {
            plan.baselineDeathTick = base.deathTick;
            plan.baselineDeathObject = base.deathObject;
        }
    }

    // Orb contact window along the no-press path (presses near orbs only).
    OrbWindow orbWin;
    bool const orbAssist = StageConfig::assistOrbs();
    if (down && orbAssist) {
        int const scanTicks = std::max(limit + model.slack + 4, limit + 1);
        orbWin = scanOrbWindow(layer, real, down, scanTicks, dt, firstDt);
        restoreCapture(layer);
        beginCapture(layer);
        if (orbWin.found) {
            plan.usedOrbWindow = true;
            AssistLog::debug(fmt::format(
                "plan: orb window [{},{}]", orbWin.first, orbWin.last
            ));
        }
    }

    int maxBranches = std::max(0, options.maxBranches);
    int effectiveCap = model.cap;

    std::vector<PlanCandidate> cands;
    cands.reserve(static_cast<size_t>(limit) + 1);
    // With orb assist: always evaluate every delay — jump-into-orb cases miss the
    // no-press contact scan, but activatedRing on a candidate still ranks correctly.
    bool const rankAll = orbAssist && down;

    for (int d = 0; d <= limit; ++d) {
        if (budgetExceeded()) {
            plan.budgetHit = true;
            if (maxBranches > 0) {
                maxBranches = 0;
                AssistLog::debug("plan: budget hit → cutting branches");
            }
            else if (effectiveCap > 24) {
                effectiveCap = std::max(24, effectiveCap / 2);
                model.cap = effectiveCap;
                AssistLog::debug(fmt::format("plan: budget hit → cap={}", effectiveCap));
            }
            else {
                AssistLog::debug("plan: budget hit → stop");
                break;
            }
        }

        ++plan.candidatesTried;

        auto r = runOne(layer, real, down, d, limit, dt, firstDt, model, -1);
        restoreCapture(layer);
        beginCapture(layer);

        if (d == 0) {
            plan.firstDeathTick = r.died ? r.deathTick : -1;
            plan.firstDeathObject = r.deathObject;
        }

        if (r.died && r.diedBeforeAction) break;

        bool ok = !r.died;
        if (r.unknown) {
            ok = true;
            plan.unknown = true;
        }

        if (!ok && r.died && !r.diedBeforeAction && down
            && (model.continuous || real->m_isRobot) && maxBranches > 0) {
            int tried = 0;
            for (int k : BRANCH_RELEASES) {
                if (tried >= maxBranches) break;
                if (budgetExceeded()) {
                    plan.budgetHit = true;
                    break;
                }
                ++tried;
                auto br = runOne(layer, real, down, d, limit, dt, firstDt, model, k);
                restoreCapture(layer);
                beginCapture(layer);
                if (!br.died || br.unknown) {
                    ok = true;
                    if (br.unknown) plan.unknown = true;
                    r = br;
                    break;
                }
            }
        }

        PlanCandidate c;
        c.delay = d;
        c.ok = ok;
        c.activatedRing = r.activatedRing;
        c.ringCount = r.ringCount;
        c.surviveTicks = r.died ? std::max(0, r.deathTick) : r.ticksRan;
        c.endX = r.endX;
        c.jumpTick = r.jumped ? r.jumpTick : d;
        c.unknown = r.unknown;

        if (!rankAll) {
            // Classic: first surviving delay wins.
            if (ok) {
                plan.jumpTick = c.jumpTick;
                plan.activatedRing = c.activatedRing;
                if (d == 0) {
                    plan.action = PlanAction::Pass;
                    plan.nowIsSafe = true;
                }
                else {
                    plan.action = PlanAction::Delay;
                    plan.delay = d;
                }
                restoreCapture(layer);
                m_simulating = false;
                return plan;
            }
        }
        else {
            cands.push_back(c);
        }
    }

    if (rankAll && !cands.empty()) {
        bool anyRing = false;
        for (auto const& c : cands) {
            if (c.activatedRing) {
                anyRing = true;
                break;
            }
        }
        // If nothing hits a ring, fall back to earliest surviving delay (classic).
        if (!anyRing && !orbWin.found) {
            for (auto const& c : cands) {
                if (!c.ok) continue;
                plan.jumpTick = c.jumpTick;
                plan.activatedRing = false;
                if (c.delay == 0) {
                    plan.action = PlanAction::Pass;
                    plan.nowIsSafe = true;
                }
                else {
                    plan.action = PlanAction::Delay;
                    plan.delay = c.delay;
                }
                restoreCapture(layer);
                m_simulating = false;
                return plan;
            }
        }
        else {
            PlanCandidate const* best = nullptr;
            int bestScore = -2000000;
            for (auto const& c : cands) {
                int const sc = candidateScore(c, orbWin, anyRing);
                if (sc > bestScore) {
                    bestScore = sc;
                    best = &c;
                }
            }
            if (best && best->ok) {
                plan.jumpTick = best->jumpTick;
                plan.activatedRing = best->activatedRing;
                plan.unknown = plan.unknown || best->unknown;
                if (best->delay == 0) {
                    plan.action = PlanAction::Pass;
                    plan.nowIsSafe = true;
                }
                else {
                    plan.action = PlanAction::Delay;
                    plan.delay = best->delay;
                }
                AssistLog::debug(fmt::format(
                    "plan: orb-rank delay +{} ring={} anyRing={} score={}",
                    best->delay, best->activatedRing, anyRing, bestScore
                ));
                restoreCapture(layer);
                m_simulating = false;
                return plan;
            }
        }
    }

    restoreCapture(layer);
    m_simulating = false;

    plan.action = PlanAction::Pass;
    AssistLog::debug(fmt::format(
        "plan: passthrough {} (no assist in {} ticks, now dies @+{})",
        down ? "press" : "release", limit, plan.firstDeathTick
    ));
    return plan;
}

SimPlan ShadowSimulator::planSequence(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    int const depth = std::clamp(options.smartDepth, 1, 3);
    if (depth <= 1 || !down) {
        return plan(layer, real, down, limit, dt, firstDt, options);
    }

    SimPlan out;
    if (!layer || !real || !m_shadow || limit < 0 || dt <= 0.f) return out;
    if (firstDt <= 0.f || firstDt > dt) firstDt = dt;

    Model model = modelFor(real, limit, options.capMul);
    out.continuous = model.continuous;
    out.smartDepthUsed = depth;

    int horizon = options.smartHorizon > 0
        ? options.smartHorizon
        : std::max(model.cap, 180);
    horizon = std::clamp(horizon, 32, 320);

    int const comboBudget = std::clamp(options.smartCombos, 8, 256);
    int const minGap = model.tapRelease ? 6 : 4;
    int const step = std::max(2, horizon / 40); // coarse grid

    auto const t0 = std::chrono::steady_clock::now();
    auto budgetExceeded = [&]() -> bool {
        if (options.budgetUs <= 0) return false;
        // Multi-jump may spend a bit more than a single plan.
        int budget = options.budgetUs;
        if (depth > 1) budget = std::max(budget, budget + budget / 2);
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        return us >= budget;
    };

    // Snapshot for replay-based sequence eval
    int const slot = captureSnapshot(real);
    if (slot < 0) {
        return plan(layer, real, down, limit, dt, firstDt, options);
    }

    // Orb window for first-click preference (cheap single scan)
    OrbWindow orbWin;
    if (StageConfig::assistOrbs()) {
        m_simulating = true;
        beginCapture(layer);
        orbWin = scanOrbWindow(
            layer, real, down, std::max(limit + model.slack + 4, limit + 1), dt, firstDt
        );
        restoreCapture(layer);
        m_simulating = false;
        if (orbWin.found) out.usedOrbWindow = true;
    }

    struct SeqBest {
        bool found = false;
        int d0 = 0;
        int score = -2000000;
        bool ring = false;
        int deathTick = -1;
        float endX = 0.f;
        bool unknown = false;
        bool survived = false;
    } best;

    auto scoreSeq = [&](int d0, bool ring, bool died, int deathTick, float endX, bool unknown) {
        int score = 0;
        if (!died || unknown) score += 100000;
        else score += deathTick * 50;
        if (ring) score += 30000;
        if (orbWin.found) {
            if (d0 >= orbWin.first && d0 <= orbWin.last) score += 15000;
            else if (!ring && d0 < orbWin.first) score -= 12000;
        }
        score += static_cast<int>(endX);
        score -= d0;
        return score;
    };

    auto evalTimeline = [&](std::vector<InputEvent> const& tl, int d0) {
        ++out.smartCombosUsed;
        auto rr = replay(layer, slot, 0, tl, dt, horizon, false, 0);
        bool ring = rr.activatedRing
            || (orbWin.found && d0 >= orbWin.first && d0 <= orbWin.last);
        int sc = scoreSeq(d0, ring, rr.died, rr.deathTick, rr.endX, rr.unknown);
        if (!best.found || sc > best.score) {
            best.found = true;
            best.d0 = d0;
            best.score = sc;
            best.ring = ring;
            best.deathTick = rr.died ? rr.deathTick : -1;
            best.endX = rr.endX;
            best.unknown = rr.unknown;
            best.survived = !rr.died || rr.unknown;
        }
    };

    auto pushTap = [&](std::vector<InputEvent>& tl, int pressTick, bool tap) {
        tl.push_back(InputEvent{ pressTick, true });
        if (tap) tl.push_back(InputEvent{ pressTick + 2, false });
    };

    bool const tap = model.tapRelease;

    // Enumerate first delays; for each, grid future clicks within combo budget.
    for (int d0 = 0; d0 <= limit; ++d0) {
        if (budgetExceeded() || out.smartCombosUsed >= comboBudget) {
            out.budgetHit = true;
            break;
        }

        if (depth == 2) {
            // At least evaluate first click alone (no second) as a candidate
            {
                std::vector<InputEvent> tl;
                pushTap(tl, d0, tap);
                evalTimeline(tl, d0);
            }
            for (int d1 = d0 + minGap; d1 < horizon; d1 += step) {
                if (budgetExceeded() || out.smartCombosUsed >= comboBudget) break;
                std::vector<InputEvent> tl;
                pushTap(tl, d0, tap);
                pushTap(tl, d1, tap);
                evalTimeline(tl, d0);
            }
        }
        else {
            // depth 3
            for (int d1 = d0 + minGap; d1 < horizon; d1 += step) {
                if (budgetExceeded() || out.smartCombosUsed >= comboBudget) break;
                {
                    std::vector<InputEvent> tl;
                    pushTap(tl, d0, tap);
                    pushTap(tl, d1, tap);
                    evalTimeline(tl, d0);
                }
                for (int d2 = d1 + minGap; d2 < horizon; d2 += step) {
                    if (budgetExceeded() || out.smartCombosUsed >= comboBudget) break;
                    std::vector<InputEvent> tl;
                    pushTap(tl, d0, tap);
                    pushTap(tl, d1, tap);
                    pushTap(tl, d2, tap);
                    evalTimeline(tl, d0);
                }
            }
        }
    }

    // Merge with single-click orb-aware plan for d0 death info + safer first pick
    PlanOptions singleOpts = options;
    singleOpts.smartDepth = 1;
    auto single = plan(layer, real, down, limit, dt, firstDt, singleOpts);
    out.firstDeathTick = single.firstDeathTick;
    out.firstDeathObject = single.firstDeathObject;
    out.baselineDeathTick = single.baselineDeathTick;
    out.baselineDeathObject = single.baselineDeathObject;
    out.continuous = single.continuous;

    if (!best.found || !best.survived) {
        // Fall back to single-click plan entirely
        single.smartDepthUsed = depth;
        single.smartCombosUsed = out.smartCombosUsed;
        single.usedOrbWindow = out.usedOrbWindow || single.usedOrbWindow;
        AssistLog::debug(fmt::format(
            "planSequence: fallback single (combos={}, depth={})",
            out.smartCombosUsed, depth
        ));
        return single;
    }

    // If single plan prefers a different first delay that hits the orb and sequence
    // agreed on survival with best.d0, keep sequence's d0; otherwise prefer orb single.
    int chosen = best.d0;
    if (single.usedOrbWindow && single.activatedRing
        && (single.action == PlanAction::Delay ? single.delay : 0) != best.d0) {
        // Prefer orb-activating single delay if sequence also survived with nearby d0
        int orbDelay = single.action == PlanAction::Delay ? single.delay : 0;
        if (std::abs(orbDelay - best.d0) <= 2) chosen = orbDelay;
    }

    out.activatedRing = best.ring || single.activatedRing;
    out.unknown = best.unknown || single.unknown;
    out.jumpTick = chosen;
    out.candidatesTried = out.smartCombosUsed;
    if (chosen == 0) {
        out.action = PlanAction::Pass;
        out.nowIsSafe = true;
        out.delay = 0;
    }
    else {
        out.action = PlanAction::Delay;
        out.delay = chosen;
        out.nowIsSafe = false;
    }

    AssistLog::debug(fmt::format(
        "planSequence: depth={} combos={} delay +{} score={} orbWin={}",
        depth, out.smartCombosUsed, chosen, best.score, out.usedOrbWindow
    ));
    return out;
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
    return out;
}

}
