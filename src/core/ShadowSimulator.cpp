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
    m_dead = false;
    m_jumped = false;
    m_unknown = false;
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

void ShadowSimulator::applyRingJump(PlayerObject* shadow, RingObject* ring) {
    if (!shadow || !ring || shadow != m_shadow) return;

    // Already used this run (and not multi-activate) → one jump only.
    bool const multi = ring->m_isMultiActivate || ring->canAllowMultiActivate();
    if (!multi && (ring->m_claimTouch || ring->m_activated)) return;
    if (!multi && shadow->m_touchedRings.count(ring->m_uniqueID)) return;

    noteActivated(ring);

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
    m_deathObject = nullptr;

    auto* s = m_shadow;
    // Press: stay released until `delay`. Release: stay held until `delay`.
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    bool airborneSeen = false;
    int groundedStreak = 0;
    int const maxTicks = delay + std::max(1, model.cap);

    for (int t = 0; t < maxTicks; ++t) {
        float const stepDt = (t == 0) ? firstDt : dt;

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
                return r;
            }
            if (m_unknown) {
                r.unknown = true;
                r.unknownTick = t;
                return r;
            }
        }

        // Input (action tick) — must run before move+collide so a jump can save.
        if (!acted && t == delay) {
            acted = true;
            if (down) s->pushButton(PlayerButton::Jump);
            else s->releaseButton(PlayerButton::Jump);
        }
        else if (acted && down) {
            if (releaseAfter >= 0 && !tapReleased && t == delay + releaseAfter) {
                tapReleased = true;
                s->releaseButton(PlayerButton::Jump);
            }
            else if (releaseAfter < 0 && model.tapRelease && !tapReleased && m_jumped) {
                // Dash orbs / still overlapping a ring: must keep holding.
                bool const touchingOrb = s->m_touchingRings && s->m_touchingRings->count() > 0;
                bool const dashing = s->m_isDashing;
                if (!touchingOrb && !dashing) {
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
            return r;
        }
        if (m_unknown) {
            r.unknown = true;
            r.unknownTick = t;
            return r;
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
                return r;
            }
        }
    }
    return r;
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
    m_deathObject = nullptr;

    auto* s = m_shadow;
    s->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    int const maxTicks = std::max(1, model.cap);
    for (int t = 0; t < maxTicks; ++t) {
        float const stepDt = (t == 0) ? firstDt : dt;
        stepPhysics(layer, s, stepDt, dt);
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

SimPlan ShadowSimulator::plan(
    PlayLayer* layer, PlayerObject* real, bool down, int limit,
    float dt, float firstDt, PlanOptions const& options
) {
    SimPlan plan;
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

    int maxBranches = std::max(0, options.maxBranches);
    int effectiveCap = model.cap;

    for (int d = 0; d <= limit; ++d) {
        if (budgetExceeded()) {
            plan.budgetHit = true;
            // First cut branches, then shorten horizon for remaining offsets.
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

        // Main branch: hold (continuous) or tap-release (cube).
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

        // Continuous / robot: if the main branch died after the action, try
        // releasing earlier — that death may not be this click's fault.
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

        if (ok) {
            plan.jumpTick = r.jumped ? r.jumpTick : d;
            if (d == 0) {
                plan.action = PlanAction::Pass;
                plan.nowIsSafe = true;
            }
            else {
                plan.action = PlanAction::Delay;
                plan.delay = d;
                if (plan.baselineDeathObject && r.deathObject
                    && plan.baselineDeathObject == r.deathObject) {
                    AssistLog::debug(fmt::format(
                        "plan: delay +{} same death object as baseline", d
                    ));
                }
            }
            restoreCapture(layer);
            m_simulating = false;
            return plan;
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
