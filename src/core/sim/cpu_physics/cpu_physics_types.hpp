#ifndef CPU_PHYSICS_TYPES_HPP
#define CPU_PHYSICS_TYPES_HPP

#include "../world/simulation_window.hpp"
#include <cstdint>
#include <vector>

class GameObject;
class PlayerObject;

// Thread-safe collision snapshot for Adaptive MT screen. Motion is real
// PlayerObject::update; collision uses GD collidedWithObjectInternal against
// these bodies (object pointers are main-thread captures, mutated under locks).
class EbCpuPhysicsBody {
public:
    static constexpr int kMaxTwSamples = 8;

    GameObject* object = nullptr;
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    int uniqueId = 0;
    int objectType = 0;
    bool isHazard = false;
    bool isRing = false;
    bool isPad = false;
    // Saved activation so each delay run can restore after GD collide mutates.
    bool savedActivated = false;
    bool savedClaimTouch = false;
    bool savedIsActivated = false;
    bool savedPoweredOn = false;
    bool savedP1 = false;
    bool savedP2 = false;
    int twSamples = 0;
    std::int16_t twTick[kMaxTwSamples]{};
    float twX[kMaxTwSamples]{};
    float twY[kMaxTwSamples]{};
    float twW[kMaxTwSamples]{};
    float twH[kMaxTwSamples]{};

private:
};

// Compact POD seed (diagnostics / world capture). GD runs reseed from template.
// Mode/body art is synced on the main thread via copyPlayerState once per screen.
class EbCpuPhysicsSeed {
public:
    float posX = 0.f;
    float posY = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float yVel = 0.f;
    float fallSpeed = 0.f;
    float gravity = 0.958972f;
    float gravityMod = 1.f;
    float playerSpeed = 1.f;
    float playerSpeedAC = 0.f;
    float vehicleSize = 1.f;
    float speedMultiplier = 0.f;
    bool upsideDown = false;
    bool sideways = false;
    bool goingLeft = false;
    bool grounded = false;
    bool grounded2 = false;
    bool onSlope = false;
    bool wasOnSlope = false;
    float slopeAngle = 0.f;
    float slopeAngleRadians = 0.f;
    float slopeVelocity = 0.f;
    bool dashing = false;
    float dashX = 0.f;
    float dashY = 0.f;
    float dashAngle = 0.f;
    float dashStartTime = 0.f;
    bool holdingJump = false;
    bool jumpBuffered = false;
    bool wasJumpBuffered = false;
    bool isShip = false;
    bool isBall = false;
    bool isBird = false;
    bool isSwing = false;
    bool isDart = false;
    bool isRobot = false;
    bool isSpider = false;
    float shipRotX = 0.f;
    float shipRotY = 0.f;
    double xVelocityRelated = 0.0;
    double xVelocityRelated2 = 0.0;
    double yVelocityRelated = 0.0;
    double yVelocityRelated3 = 0.0;
    float totalTime = 0.f;
    float lastJumpTime = 0.f;
    float lastLandTime = 0.f;
    bool hasEverJumped = false;
    bool hasEverHitRing = false;
    bool valid = false;

private:
};

class EbCpuPhysicsWorld {
public:
    EbSimulationWindow window;
    EbCpuPhysicsSeed seed;
    std::vector<EbCpuPhysicsBody> bodies;
    float floorY = 0.f;
    float ceilY = 9999.f;
    int horizonTicks = 48;
    float simDt = 1.f / 240.f;
    int baseAbsTick = 0;
    bool valid = false;

private:
};

class EbCpuPhysicsRunResult {
public:
    bool died = false;
    bool unknown = false;
    bool jumped = false;
    bool activatedBoost = false;
    bool diedBeforeAction = false;
    int ticksRan = 0;
    int deathTick = -1;
    int jumpTick = -1;
    float endX = 0.f;
    float deathX = 0.f;
    float endY = 0.f;

private:
};

#endif
