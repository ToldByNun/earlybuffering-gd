#include "shadow_player_state.hpp"
#include "../shadow_simulator.hpp"

void EbShadowPlayerState::copyWaveTrail(EbShadowSimulator& simulator, PlayerObject* destination,
                                      PlayerObject* source) {
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

void EbShadowPlayerState::clearCollisionLogs(PlayerObject* state) {
    state->m_collisionLogTop->removeAllObjects();
    state->m_collisionLogBottom->removeAllObjects();
    state->m_collisionLogLeft->removeAllObjects();
    state->m_collisionLogRight->removeAllObjects();
    state->m_lastCollisionTop = -1;
    state->m_lastCollisionBottom = -1;
    state->m_lastCollisionLeft = -1;
    state->m_lastCollisionRight = -1;
}

void EbShadowPlayerState::copyPlayerState(PlayerObject* destination, PlayerObject* source) {
    if (!destination || !source)
        return;

    destination->copyAttributes(source);
    destination->setPosition(source->getPosition());
    destination->setRotation(source->getRotation());
    destination->setScaleX(source->getScaleX());
    destination->setScaleY(source->getScaleY());
    EbShadowSimulator::hideGhost(destination);

    destination->m_position = source->m_position;
    destination->m_yVelocity = source->m_yVelocity;
    destination->m_fallSpeed = source->m_fallSpeed;
    destination->m_gravity = source->m_gravity;
    destination->m_gravityMod = source->m_gravityMod;
    destination->m_speedMultiplier = source->m_speedMultiplier;
    destination->m_yStart = source->m_yStart;
    destination->m_playerSpeed = source->m_playerSpeed;
    destination->m_playerSpeedAC = source->m_playerSpeedAC;
    destination->m_vehicleSize = source->m_vehicleSize;
    destination->m_isUpsideDown = source->m_isUpsideDown;
    destination->m_isSideways = source->m_isSideways;
    destination->m_isGoingLeft = source->m_isGoingLeft;
    destination->m_isShip = source->m_isShip;
    destination->m_isBall = source->m_isBall;
    destination->m_isBird = source->m_isBird;
    destination->m_isSwing = source->m_isSwing;
    destination->m_isDart = source->m_isDart;
    destination->m_isRobot = source->m_isRobot;
    destination->m_isSpider = source->m_isSpider;
    destination->m_isSecondPlayer = source->m_isSecondPlayer;
    destination->m_isPlatformer = source->m_isPlatformer;
    destination->m_platformerXVelocity = source->m_platformerXVelocity;
    destination->m_platformerVelocityRelated = source->m_platformerVelocityRelated;
    destination->m_xVelocityRelated = source->m_xVelocityRelated;
    destination->m_xVelocityRelated2 = source->m_xVelocityRelated2;
    destination->m_yVelocityRelated = source->m_yVelocityRelated;
    destination->m_yVelocityRelated3 = source->m_yVelocityRelated3;
    destination->m_reverseRelated = source->m_reverseRelated;
    destination->m_maybeReverseSpeed = source->m_maybeReverseSpeed;
    destination->m_maybeReverseAcceleration = source->m_maybeReverseAcceleration;
    destination->m_reverseSync = source->m_reverseSync;
    destination->m_wasTeleported = source->m_wasTeleported;
    destination->m_fixGravityBug = source->m_fixGravityBug;
    destination->m_shipRotation = source->m_shipRotation;
    destination->m_accelerationOrSpeed = source->m_accelerationOrSpeed;
    destination->m_isAccelerating = source->m_isAccelerating;
    destination->m_maybeIsFalling = source->m_maybeIsFalling;
    destination->m_maybeIsBoosted = source->m_maybeIsBoosted;
    destination->m_decreaseBoostSlide = source->m_decreaseBoostSlide;
    destination->m_physDeltaRelated = source->m_physDeltaRelated;
    destination->m_totalTime = source->m_totalTime;
    destination->m_gameModeChangedTime = source->m_gameModeChangedTime;
    destination->m_lastMovedTime = source->m_lastMovedTime;

    destination->m_isDashing = source->m_isDashing;
    destination->m_dashX = source->m_dashX;
    destination->m_dashY = source->m_dashY;
    destination->m_dashAngle = source->m_dashAngle;
    destination->m_dashStartTime = source->m_dashStartTime;
    destination->m_dashRing = source->m_dashRing;

    destination->m_isOnGround = source->m_isOnGround;
    destination->m_isOnGround2 = source->m_isOnGround2;
    destination->m_isOnGround3 = source->m_isOnGround3;
    destination->m_isOnGround4 = source->m_isOnGround4;
    destination->m_lastGroundObject = source->m_lastGroundObject;
    destination->m_maybeLastGroundObject = source->m_maybeLastGroundObject;
    destination->m_preLastGroundObject = source->m_preLastGroundObject;
    destination->m_lastGroundedPos = source->m_lastGroundedPos;
    destination->m_groundYVelocity = source->m_groundYVelocity;
    destination->m_groundObjectMaterial = source->m_groundObjectMaterial;
    destination->m_lastLandTime = source->m_lastLandTime;
    destination->m_lastJumpTime = source->m_lastJumpTime;
    destination->m_lastFlipTime = source->m_lastFlipTime;
    destination->m_lastSpiderFlipTime = source->m_lastSpiderFlipTime;
    destination->m_collidedObject = source->m_collidedObject;
    destination->m_collidingWithLeft = source->m_collidingWithLeft;
    destination->m_collidingWithRight = source->m_collidingWithRight;
    destination->m_lastCollisionBottom = source->m_lastCollisionBottom;
    destination->m_lastCollisionTop = source->m_lastCollisionTop;
    destination->m_lastCollisionLeft = source->m_lastCollisionLeft;
    destination->m_lastCollisionRight = source->m_lastCollisionRight;
    destination->m_collidedTopMinY = source->m_collidedTopMinY;
    destination->m_collidedBottomMaxY = source->m_collidedBottomMaxY;
    destination->m_collidedLeftMaxX = source->m_collidedLeftMaxX;
    destination->m_collidedRightMinX = source->m_collidedRightMinX;
    destination->m_maybeIsColliding = source->m_maybeIsColliding;
    destination->m_objectSnappedTo = source->m_objectSnappedTo;
    destination->m_snapDistance = source->m_snapDistance;
    destination->m_maybeTouchedBreakableBlock = source->m_maybeTouchedBreakableBlock;
    destination->m_isOnIce = source->m_isOnIce;
    destination->m_isSliding = source->m_isSliding;
    destination->m_isSlidingRight = source->m_isSlidingRight;
    destination->m_maybeSlidingTime = source->m_maybeSlidingTime;
    destination->m_maybeSlidingStartTime = source->m_maybeSlidingStartTime;
    destination->m_changedDirectionsTime = source->m_changedDirectionsTime;
    destination->m_maybeChangedDirectionAngle = source->m_maybeChangedDirectionAngle;
    destination->m_isMoving = source->m_isMoving;
    destination->m_maybeHasStopped = source->m_maybeHasStopped;
    destination->m_scaleXRelated = source->m_scaleXRelated;
    destination->m_scaleXRelated2 = source->m_scaleXRelated2;
    destination->m_scaleXRelated3 = source->m_scaleXRelated3;
    destination->m_scaleXRelated4 = source->m_scaleXRelated4;
    destination->m_scaleXRelated5 = source->m_scaleXRelated5;
    destination->m_scaleXRelatedTime = source->m_scaleXRelatedTime;

    destination->m_isOnSlope = source->m_isOnSlope;
    destination->m_wasOnSlope = source->m_wasOnSlope;
    destination->m_slopeVelocity = source->m_slopeVelocity;
    destination->m_slopeAngle = source->m_slopeAngle;
    destination->m_slopeAngleRadians = source->m_slopeAngleRadians;
    destination->m_slopeStartTime = source->m_slopeStartTime;
    destination->m_slopeEndTime = source->m_slopeEndTime;
    destination->m_slopeRotation = source->m_slopeRotation;
    destination->m_currentSlopeYVelocity = source->m_currentSlopeYVelocity;
    destination->m_yVelocityBeforeSlope = source->m_yVelocityBeforeSlope;
    destination->m_currentSlope = source->m_currentSlope;
    destination->m_currentSlope2 = source->m_currentSlope2;
    destination->m_currentPotentialSlope = source->m_currentPotentialSlope;
    destination->m_potentialSlopeMap = source->m_potentialSlopeMap;
    destination->m_isCollidingWithSlope = source->m_isCollidingWithSlope;
    destination->m_collidingWithSlopeId = source->m_collidingWithSlopeId;
    destination->m_slopeFlipGravityRelated = source->m_slopeFlipGravityRelated;
    destination->m_slopeSlidingMaybeRotated = source->m_slopeSlidingMaybeRotated;
    destination->m_isCurrentSlopeTop = source->m_isCurrentSlopeTop;
    destination->m_maybeUpsideDownSlope = source->m_maybeUpsideDownSlope;
    destination->m_maybeSlopeForce = source->m_maybeSlopeForce;
    destination->m_maybeGoingCorrectSlopeDirection = source->m_maybeGoingCorrectSlopeDirection;

    destination->m_wasJumpBuffered = source->m_wasJumpBuffered;
    destination->m_wasRobotJump = source->m_wasRobotJump;
    destination->m_fixRobotJump = source->m_fixRobotJump;
    destination->m_stateJumpBuffered = source->m_stateJumpBuffered;
    destination->m_stateRingJump = source->m_stateRingJump;
    destination->m_stateRingJump2 = source->m_stateRingJump2;
    destination->m_touchedRing = source->m_touchedRing;
    destination->m_touchedCustomRing = source->m_touchedCustomRing;
    destination->m_touchedGravityPortal = source->m_touchedGravityPortal;
    destination->m_touchedPad = source->m_touchedPad;
    destination->m_padRingRelated = source->m_padRingRelated;
    destination->m_ringJumpRelated = source->m_ringJumpRelated;
    destination->m_ringRelatedSet = source->m_ringRelatedSet;
    destination->m_jumpPadRelated = source->m_jumpPadRelated;
    destination->m_blackOrbRelated = source->m_blackOrbRelated;
    destination->m_hasEverJumped = source->m_hasEverJumped;
    destination->m_hasEverHitRing = source->m_hasEverHitRing;
    destination->m_lastPortalPos = source->m_lastPortalPos;
    destination->m_lastActivatedPortal = source->m_lastActivatedPortal;
    destination->m_touchedRings = source->m_touchedRings;

    destination->m_stateOnGround = source->m_stateOnGround;
    destination->m_stateUnk = source->m_stateUnk;
    destination->m_stateNoStickX = source->m_stateNoStickX;
    destination->m_stateNoStickY = source->m_stateNoStickY;
    destination->m_stateUnk2 = source->m_stateUnk2;
    destination->m_stateBoostX = source->m_stateBoostX;
    destination->m_stateBoostY = source->m_stateBoostY;
    destination->m_maybeStateForce2 = source->m_maybeStateForce2;
    destination->m_stateScale = source->m_stateScale;
    destination->m_stateNoAutoJump = source->m_stateNoAutoJump;
    destination->m_stateDartSlide = source->m_stateDartSlide;
    destination->m_stateHitHead = source->m_stateHitHead;
    destination->m_stateFlipGravity = source->m_stateFlipGravity;
    destination->m_stateForce = source->m_stateForce;
    destination->m_stateForceVector = source->m_stateForceVector;
    destination->m_affectedByForces = source->m_affectedByForces;

    destination->m_isLocked = source->m_isLocked;
    destination->m_controlsDisabled = source->m_controlsDisabled;
    destination->m_inputsLocked = source->m_inputsLocked;
    destination->m_holdingRight = source->m_holdingRight;
    destination->m_holdingLeft = source->m_holdingLeft;
    destination->m_leftPressedFirst = source->m_leftPressedFirst;

    destination->m_jumpBuffered = source->m_jumpBuffered;
    destination->m_holdingButtons = source->m_holdingButtons;
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

    EbShadowSimulator::clearCollisionLogs(destination);

    if (source->m_isDart) {
        // local helper via instance method — static caller uses get()
        EbShadowSimulator::get().copyWaveTrail(destination, source);
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
    destination->m_isOnGround = source->m_isOnGround;
    destination->m_isOnGround2 = source->m_isOnGround2;
    destination->m_isOnGround3 = source->m_isOnGround3;
    destination->m_isOnGround4 = source->m_isOnGround4;
    destination->m_isOnSlope = source->m_isOnSlope;
    destination->m_wasOnSlope = source->m_wasOnSlope;
    destination->m_currentSlope = source->m_currentSlope;
    destination->m_currentSlope2 = source->m_currentSlope2;
    destination->m_stateOnGround = source->m_stateOnGround;
    destination->m_jumpBuffered = source->m_jumpBuffered;
    destination->m_wasJumpBuffered = source->m_wasJumpBuffered;
    destination->m_stateJumpBuffered = source->m_stateJumpBuffered;
    destination->m_stateRingJump = source->m_stateRingJump;
    destination->m_stateRingJump2 = source->m_stateRingJump2;
    destination->m_hasEverJumped = source->m_hasEverJumped;
    destination->m_hasEverHitRing = source->m_hasEverHitRing;
    destination->m_holdingButtons = source->m_holdingButtons;

    EbShadowSimulator::hideGhost(destination);
    EbShadowSimulator::clearCollisionLogs(destination);
}

size_t EbShadowPlayerState::diagCaptureTransformCount(EbShadowSimulator& simulator, PlayLayer* layer) {
    if (!layer)
        return 0;
    simulator.beginCapture(layer);
    size_t const count = simulator.m_savedTransforms.size();
    simulator.restoreCapture(layer);
    return count;
}

int EbShadowPlayerState::captureStartSnapshot(EbShadowSimulator& simulator, PlayerObject* real) {
    if (!real || !simulator.m_snapshots[EbShadowSimulator::START_SNAPSHOT_SLOT])
        return -1;
    simulator.copyPlayerState(simulator.m_snapshots[EbShadowSimulator::START_SNAPSHOT_SLOT], real);
    return EbShadowSimulator::START_SNAPSHOT_SLOT;
}

int EbShadowPlayerState::captureSnapshot(EbShadowSimulator& simulator, PlayerObject* real) {
    if (!real || !simulator.m_snapshots[0])
        return -1;
    // Never clobber attempt-start (bake) or parity anchors (14–15).
    if (simulator.m_nextSnapshot <= EbShadowSimulator::START_SNAPSHOT_SLOT ||
        simulator.m_nextSnapshot >= EbShadowSimulator::SNAPSHOT_RING_END) {
        simulator.m_nextSnapshot = EbShadowSimulator::SNAPSHOT_RING_BEGIN;
    }
    int const slot = simulator.m_nextSnapshot;
    ++simulator.m_nextSnapshot;
    if (simulator.m_nextSnapshot >= EbShadowSimulator::SNAPSHOT_RING_END) {
        simulator.m_nextSnapshot = EbShadowSimulator::SNAPSHOT_RING_BEGIN;
    }
    simulator.copyPlayerState(simulator.m_snapshots[slot], real);
    return slot;
}

int EbShadowPlayerState::captureParitySnapshot(EbShadowSimulator& simulator, PlayerObject* real,
                                             int which) {
    if (!real || !simulator.m_snapshots[0])
        return -1;
    int const slot = (which & 1) ? EbShadowSimulator::PARITY_SLOT_B : EbShadowSimulator::PARITY_SLOT_A;
    if (!simulator.m_snapshots[slot])
        return -1;
    simulator.copyPlayerState(simulator.m_snapshots[slot], real);
    return slot;
}

PlayerObject* EbShadowPlayerState::snapshotSlot(EbShadowSimulator const& simulator, int slot) {
    if (!(slot < 0 || slot >= EbShadowSimulator::SNAPSHOT_SLOTS)) {
        return simulator.m_snapshots[slot];
    }
    return nullptr;
}
