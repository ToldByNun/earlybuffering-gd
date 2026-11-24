#include "cpu_physics_seed.hpp"
#include <Geode/Geode.hpp>
#include <cmath>
#include <fmt/format.h>

EbCpuPhysicsSeed EbCpuPhysicsSeedIo::capture(PlayerObject* source) {
    EbCpuPhysicsSeed seed;
    if (!source)
        return seed;
    auto const pos = source->getPosition();
    seed.posX = pos.x;
    seed.posY = pos.y;
    seed.rotation = source->getRotation();
    seed.scaleX = source->getScaleX();
    seed.scaleY = source->getScaleY();
    seed.yVel = static_cast<float>(source->m_yVelocity);
    seed.fallSpeed = source->m_fallSpeed;
    seed.gravity = source->m_gravity;
    seed.gravityMod = source->m_gravityMod;
    seed.playerSpeed = source->m_playerSpeed;
    seed.playerSpeedAC = source->m_playerSpeedAC;
    seed.vehicleSize = source->m_vehicleSize;
    seed.speedMultiplier = source->m_speedMultiplier;
    seed.upsideDown = source->m_isUpsideDown;
    seed.sideways = source->m_isSideways;
    seed.goingLeft = source->m_isGoingLeft;
    seed.grounded = source->m_isOnGround;
    seed.grounded2 = source->m_isOnGround2;
    seed.onSlope = source->m_isOnSlope;
    seed.wasOnSlope = source->m_wasOnSlope;
    seed.slopeAngle = source->m_slopeAngle;
    seed.slopeAngleRadians = source->m_slopeAngleRadians;
    seed.slopeVelocity = source->m_slopeVelocity;
    seed.dashing = source->m_isDashing;
    seed.dashX = source->m_dashX;
    seed.dashY = source->m_dashY;
    seed.dashAngle = source->m_dashAngle;
    seed.dashStartTime = source->m_dashStartTime;
    seed.holdingJump = source->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
    seed.jumpBuffered = source->m_jumpBuffered;
    seed.wasJumpBuffered = source->m_wasJumpBuffered;
    seed.isShip = source->m_isShip;
    seed.isBall = source->m_isBall;
    seed.isBird = source->m_isBird;
    seed.isSwing = source->m_isSwing;
    seed.isDart = source->m_isDart;
    seed.isRobot = source->m_isRobot;
    seed.isSpider = source->m_isSpider;
    seed.shipRotX = source->m_shipRotation.x;
    seed.shipRotY = source->m_shipRotation.y;
    seed.xVelocityRelated = source->m_xVelocityRelated;
    seed.xVelocityRelated2 = source->m_xVelocityRelated2;
    seed.yVelocityRelated = source->m_yVelocityRelated;
    seed.yVelocityRelated3 = source->m_yVelocityRelated3;
    seed.totalTime = source->m_totalTime;
    seed.lastJumpTime = source->m_lastJumpTime;
    seed.lastLandTime = source->m_lastLandTime;
    seed.hasEverJumped = source->m_hasEverJumped;
    seed.hasEverHitRing = source->m_hasEverHitRing;
    seed.valid = true;
    return seed;
}

void EbCpuPhysicsSeedIo::apply(PlayerObject* destination, EbCpuPhysicsSeed const& seed) {
    if (!destination || !seed.valid)
        return;

    destination->setPosition({seed.posX, seed.posY});
    destination->m_position = cocos2d::CCPoint{seed.posX, seed.posY};
    destination->setRotation(seed.rotation);
    destination->setScaleX(seed.scaleX);
    destination->setScaleY(seed.scaleY);

    destination->m_yVelocity = seed.yVel;
    destination->m_fallSpeed = seed.fallSpeed;
    destination->m_gravity = seed.gravity;
    destination->m_gravityMod = seed.gravityMod;
    destination->m_playerSpeed = seed.playerSpeed;
    destination->m_playerSpeedAC = seed.playerSpeedAC;
    destination->m_vehicleSize = seed.vehicleSize;
    destination->m_speedMultiplier = seed.speedMultiplier;
    destination->m_isUpsideDown = seed.upsideDown;
    destination->m_isSideways = seed.sideways;
    destination->m_isGoingLeft = seed.goingLeft;
    destination->m_isOnGround = seed.grounded;
    destination->m_isOnGround2 = seed.grounded2;
    destination->m_isOnSlope = seed.onSlope;
    destination->m_wasOnSlope = seed.wasOnSlope;
    destination->m_slopeAngle = seed.slopeAngle;
    destination->m_slopeAngleRadians = seed.slopeAngleRadians;
    destination->m_slopeVelocity = seed.slopeVelocity;
    destination->m_isDashing = seed.dashing;
    destination->m_dashX = seed.dashX;
    destination->m_dashY = seed.dashY;
    destination->m_dashAngle = seed.dashAngle;
    destination->m_dashStartTime = seed.dashStartTime;
    destination->m_jumpBuffered = seed.jumpBuffered;
    destination->m_wasJumpBuffered = seed.wasJumpBuffered;
    destination->m_isShip = seed.isShip;
    destination->m_isBall = seed.isBall;
    destination->m_isBird = seed.isBird;
    destination->m_isSwing = seed.isSwing;
    destination->m_isDart = seed.isDart;
    destination->m_isRobot = seed.isRobot;
    destination->m_isSpider = seed.isSpider;
    destination->m_shipRotation = cocos2d::CCPoint{seed.shipRotX, seed.shipRotY};
    destination->m_xVelocityRelated = seed.xVelocityRelated;
    destination->m_xVelocityRelated2 = seed.xVelocityRelated2;
    destination->m_yVelocityRelated = seed.yVelocityRelated;
    destination->m_yVelocityRelated3 = seed.yVelocityRelated3;
    destination->m_totalTime = seed.totalTime;
    destination->m_lastJumpTime = seed.lastJumpTime;
    destination->m_lastLandTime = seed.lastLandTime;
    destination->m_hasEverJumped = seed.hasEverJumped;
    destination->m_hasEverHitRing = seed.hasEverHitRing;
    destination->m_isDead = false;
    destination->m_playEffects = false;

    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    destination->m_holdingButtons[jumpIdx] = seed.holdingJump;

    if (destination->m_touchingRings)
        destination->m_touchingRings->removeAllObjects();
    destination->m_touchedRings.clear();
    destination->m_touchedRing = false;
    destination->m_touchedPad = false;
}

void EbCpuPhysicsSeedIo::reseedFrom(PlayerObject* destination, PlayerObject* templateGhost) {
    if (!destination || !templateGhost)
        return;

    // Mirror EbShadowPlayerState::copyPlayerState field restore without Cocos
    // alloc / switchedToMode. Mode and collide rect must already match via the
    // main-thread seedFrom(copyPlayerState) pass before the parallel batch.
    destination->setPosition(templateGhost->getPosition());
    destination->setRotation(templateGhost->getRotation());
    destination->setScaleX(templateGhost->getScaleX());
    destination->setScaleY(templateGhost->getScaleY());
    destination->setContentSize(templateGhost->getContentSize());

    destination->m_position = templateGhost->m_position;
    destination->m_yVelocity = templateGhost->m_yVelocity;
    destination->m_fallSpeed = templateGhost->m_fallSpeed;
    destination->m_gravity = templateGhost->m_gravity;
    destination->m_gravityMod = templateGhost->m_gravityMod;
    destination->m_speedMultiplier = templateGhost->m_speedMultiplier;
    destination->m_yStart = templateGhost->m_yStart;
    destination->m_playerSpeed = templateGhost->m_playerSpeed;
    destination->m_playerSpeedAC = templateGhost->m_playerSpeedAC;
    destination->m_vehicleSize = templateGhost->m_vehicleSize;
    destination->m_isUpsideDown = templateGhost->m_isUpsideDown;
    destination->m_isSideways = templateGhost->m_isSideways;
    destination->m_isGoingLeft = templateGhost->m_isGoingLeft;
    destination->m_isShip = templateGhost->m_isShip;
    destination->m_isBall = templateGhost->m_isBall;
    destination->m_isBird = templateGhost->m_isBird;
    destination->m_isSwing = templateGhost->m_isSwing;
    destination->m_isDart = templateGhost->m_isDart;
    destination->m_isRobot = templateGhost->m_isRobot;
    destination->m_isSpider = templateGhost->m_isSpider;
    destination->m_isSecondPlayer = templateGhost->m_isSecondPlayer;
    destination->m_isPlatformer = templateGhost->m_isPlatformer;
    destination->m_platformerXVelocity = templateGhost->m_platformerXVelocity;
    destination->m_platformerVelocityRelated = templateGhost->m_platformerVelocityRelated;
    destination->m_xVelocityRelated = templateGhost->m_xVelocityRelated;
    destination->m_xVelocityRelated2 = templateGhost->m_xVelocityRelated2;
    destination->m_yVelocityRelated = templateGhost->m_yVelocityRelated;
    destination->m_yVelocityRelated3 = templateGhost->m_yVelocityRelated3;
    destination->m_reverseRelated = templateGhost->m_reverseRelated;
    destination->m_maybeReverseSpeed = templateGhost->m_maybeReverseSpeed;
    destination->m_maybeReverseAcceleration = templateGhost->m_maybeReverseAcceleration;
    destination->m_reverseSync = templateGhost->m_reverseSync;
    destination->m_wasTeleported = templateGhost->m_wasTeleported;
    destination->m_fixGravityBug = templateGhost->m_fixGravityBug;
    destination->m_shipRotation = templateGhost->m_shipRotation;
    destination->m_accelerationOrSpeed = templateGhost->m_accelerationOrSpeed;
    destination->m_isAccelerating = templateGhost->m_isAccelerating;
    destination->m_maybeIsFalling = templateGhost->m_maybeIsFalling;
    destination->m_maybeIsBoosted = templateGhost->m_maybeIsBoosted;
    destination->m_decreaseBoostSlide = templateGhost->m_decreaseBoostSlide;
    destination->m_physDeltaRelated = templateGhost->m_physDeltaRelated;
    destination->m_totalTime = templateGhost->m_totalTime;
    destination->m_gameModeChangedTime = templateGhost->m_gameModeChangedTime;
    destination->m_lastMovedTime = templateGhost->m_lastMovedTime;

    destination->m_isDashing = templateGhost->m_isDashing;
    destination->m_dashX = templateGhost->m_dashX;
    destination->m_dashY = templateGhost->m_dashY;
    destination->m_dashAngle = templateGhost->m_dashAngle;
    destination->m_dashStartTime = templateGhost->m_dashStartTime;
    destination->m_dashRing = templateGhost->m_dashRing;

    destination->m_isOnGround = templateGhost->m_isOnGround;
    destination->m_isOnGround2 = templateGhost->m_isOnGround2;
    destination->m_isOnGround3 = templateGhost->m_isOnGround3;
    destination->m_isOnGround4 = templateGhost->m_isOnGround4;
    destination->m_lastGroundObject = templateGhost->m_lastGroundObject;
    destination->m_maybeLastGroundObject = templateGhost->m_maybeLastGroundObject;
    destination->m_preLastGroundObject = templateGhost->m_preLastGroundObject;
    destination->m_lastGroundedPos = templateGhost->m_lastGroundedPos;
    destination->m_groundYVelocity = templateGhost->m_groundYVelocity;
    destination->m_groundObjectMaterial = templateGhost->m_groundObjectMaterial;
    destination->m_lastLandTime = templateGhost->m_lastLandTime;
    destination->m_lastJumpTime = templateGhost->m_lastJumpTime;
    destination->m_lastFlipTime = templateGhost->m_lastFlipTime;
    destination->m_lastSpiderFlipTime = templateGhost->m_lastSpiderFlipTime;
    destination->m_collidedObject = templateGhost->m_collidedObject;
    destination->m_collidingWithLeft = templateGhost->m_collidingWithLeft;
    destination->m_collidingWithRight = templateGhost->m_collidingWithRight;
    destination->m_lastCollisionBottom = templateGhost->m_lastCollisionBottom;
    destination->m_lastCollisionTop = templateGhost->m_lastCollisionTop;
    destination->m_lastCollisionLeft = templateGhost->m_lastCollisionLeft;
    destination->m_lastCollisionRight = templateGhost->m_lastCollisionRight;
    destination->m_collidedTopMinY = templateGhost->m_collidedTopMinY;
    destination->m_collidedBottomMaxY = templateGhost->m_collidedBottomMaxY;
    destination->m_collidedLeftMaxX = templateGhost->m_collidedLeftMaxX;
    destination->m_collidedRightMinX = templateGhost->m_collidedRightMinX;
    destination->m_maybeIsColliding = templateGhost->m_maybeIsColliding;
    destination->m_objectSnappedTo = templateGhost->m_objectSnappedTo;
    destination->m_snapDistance = templateGhost->m_snapDistance;
    destination->m_maybeTouchedBreakableBlock = templateGhost->m_maybeTouchedBreakableBlock;
    destination->m_isOnIce = templateGhost->m_isOnIce;
    destination->m_isSliding = templateGhost->m_isSliding;
    destination->m_isSlidingRight = templateGhost->m_isSlidingRight;
    destination->m_maybeSlidingTime = templateGhost->m_maybeSlidingTime;
    destination->m_maybeSlidingStartTime = templateGhost->m_maybeSlidingStartTime;
    destination->m_changedDirectionsTime = templateGhost->m_changedDirectionsTime;
    destination->m_maybeChangedDirectionAngle = templateGhost->m_maybeChangedDirectionAngle;
    destination->m_isMoving = templateGhost->m_isMoving;
    destination->m_maybeHasStopped = templateGhost->m_maybeHasStopped;
    destination->m_scaleXRelated = templateGhost->m_scaleXRelated;
    destination->m_scaleXRelated2 = templateGhost->m_scaleXRelated2;
    destination->m_scaleXRelated3 = templateGhost->m_scaleXRelated3;
    destination->m_scaleXRelated4 = templateGhost->m_scaleXRelated4;
    destination->m_scaleXRelated5 = templateGhost->m_scaleXRelated5;
    destination->m_scaleXRelatedTime = templateGhost->m_scaleXRelatedTime;

    destination->m_isOnSlope = templateGhost->m_isOnSlope;
    destination->m_wasOnSlope = templateGhost->m_wasOnSlope;
    destination->m_slopeVelocity = templateGhost->m_slopeVelocity;
    destination->m_slopeAngle = templateGhost->m_slopeAngle;
    destination->m_slopeAngleRadians = templateGhost->m_slopeAngleRadians;
    destination->m_slopeStartTime = templateGhost->m_slopeStartTime;
    destination->m_slopeEndTime = templateGhost->m_slopeEndTime;
    destination->m_slopeRotation = templateGhost->m_slopeRotation;
    destination->m_currentSlopeYVelocity = templateGhost->m_currentSlopeYVelocity;
    destination->m_yVelocityBeforeSlope = templateGhost->m_yVelocityBeforeSlope;
    destination->m_currentSlope = templateGhost->m_currentSlope;
    destination->m_currentSlope2 = templateGhost->m_currentSlope2;
    destination->m_currentPotentialSlope = templateGhost->m_currentPotentialSlope;
    destination->m_potentialSlopeMap = templateGhost->m_potentialSlopeMap;
    destination->m_isCollidingWithSlope = templateGhost->m_isCollidingWithSlope;
    destination->m_collidingWithSlopeId = templateGhost->m_collidingWithSlopeId;
    destination->m_slopeFlipGravityRelated = templateGhost->m_slopeFlipGravityRelated;
    destination->m_slopeSlidingMaybeRotated = templateGhost->m_slopeSlidingMaybeRotated;
    destination->m_isCurrentSlopeTop = templateGhost->m_isCurrentSlopeTop;
    destination->m_maybeUpsideDownSlope = templateGhost->m_maybeUpsideDownSlope;
    destination->m_maybeSlopeForce = templateGhost->m_maybeSlopeForce;
    destination->m_maybeGoingCorrectSlopeDirection = templateGhost->m_maybeGoingCorrectSlopeDirection;

    destination->m_wasJumpBuffered = templateGhost->m_wasJumpBuffered;
    destination->m_wasRobotJump = templateGhost->m_wasRobotJump;
    destination->m_fixRobotJump = templateGhost->m_fixRobotJump;
    destination->m_stateJumpBuffered = templateGhost->m_stateJumpBuffered;
    destination->m_stateRingJump = templateGhost->m_stateRingJump;
    destination->m_stateRingJump2 = templateGhost->m_stateRingJump2;
    destination->m_touchedRing = templateGhost->m_touchedRing;
    destination->m_touchedCustomRing = templateGhost->m_touchedCustomRing;
    destination->m_touchedGravityPortal = templateGhost->m_touchedGravityPortal;
    destination->m_touchedPad = templateGhost->m_touchedPad;
    destination->m_padRingRelated = templateGhost->m_padRingRelated;
    destination->m_ringJumpRelated = templateGhost->m_ringJumpRelated;
    destination->m_ringRelatedSet = templateGhost->m_ringRelatedSet;
    destination->m_jumpPadRelated = templateGhost->m_jumpPadRelated;
    destination->m_blackOrbRelated = templateGhost->m_blackOrbRelated;
    destination->m_hasEverJumped = templateGhost->m_hasEverJumped;
    destination->m_hasEverHitRing = templateGhost->m_hasEverHitRing;
    destination->m_lastPortalPos = templateGhost->m_lastPortalPos;
    destination->m_lastActivatedPortal = templateGhost->m_lastActivatedPortal;
    destination->m_touchedRings = templateGhost->m_touchedRings;

    destination->m_stateOnGround = templateGhost->m_stateOnGround;
    destination->m_stateUnk = templateGhost->m_stateUnk;
    destination->m_stateNoStickX = templateGhost->m_stateNoStickX;
    destination->m_stateNoStickY = templateGhost->m_stateNoStickY;
    destination->m_stateUnk2 = templateGhost->m_stateUnk2;
    destination->m_stateBoostX = templateGhost->m_stateBoostX;
    destination->m_stateBoostY = templateGhost->m_stateBoostY;
    destination->m_maybeStateForce2 = templateGhost->m_maybeStateForce2;
    destination->m_stateScale = templateGhost->m_stateScale;
    destination->m_stateNoAutoJump = templateGhost->m_stateNoAutoJump;
    destination->m_stateDartSlide = templateGhost->m_stateDartSlide;
    destination->m_stateHitHead = templateGhost->m_stateHitHead;
    destination->m_stateFlipGravity = templateGhost->m_stateFlipGravity;
    destination->m_stateForce = templateGhost->m_stateForce;
    destination->m_stateForceVector = templateGhost->m_stateForceVector;
    destination->m_affectedByForces = templateGhost->m_affectedByForces;

    destination->m_isLocked = templateGhost->m_isLocked;
    destination->m_controlsDisabled = templateGhost->m_controlsDisabled;
    destination->m_inputsLocked = templateGhost->m_inputsLocked;
    destination->m_holdingRight = templateGhost->m_holdingRight;
    destination->m_holdingLeft = templateGhost->m_holdingLeft;
    destination->m_leftPressedFirst = templateGhost->m_leftPressedFirst;

    destination->m_jumpBuffered = templateGhost->m_jumpBuffered;
    destination->m_holdingButtons = templateGhost->m_holdingButtons;
    destination->m_isDead = false;
    destination->m_playEffects = false;

    if (destination->m_touchingRings) {
        destination->m_touchingRings->removeAllObjects();
        if (templateGhost->m_touchingRings) {
            for (unsigned index = 0; index < templateGhost->m_touchingRings->count(); ++index) {
                destination->m_touchingRings->addObject(
                    templateGhost->m_touchingRings->objectAtIndex(index));
            }
        }
    }

    if (destination->m_collisionLogTop)
        destination->m_collisionLogTop->removeAllObjects();
    if (destination->m_collisionLogBottom)
        destination->m_collisionLogBottom->removeAllObjects();
    if (destination->m_collisionLogLeft)
        destination->m_collisionLogLeft->removeAllObjects();
    if (destination->m_collisionLogRight)
        destination->m_collisionLogRight->removeAllObjects();
    destination->m_lastCollisionTop = -1;
    destination->m_lastCollisionBottom = -1;
    destination->m_lastCollisionLeft = -1;
    destination->m_lastCollisionRight = -1;
}

bool EbCpuPhysicsSeedIo::selfCheck(std::string& report) {
    report.clear();
    EbCpuPhysicsSeed empty;
    if (empty.valid) {
        report = "CpuPhysicsSeed selfCheck fails: empty-valid";
        return false;
    }
    report = "CpuPhysicsSeed selfCheck OK";
    return true;
}
