#ifndef CPU_PHYSICS_TYPES_HPP
#define CPU_PHYSICS_TYPES_HPP

#include <cstdint>
#include <vector>

// Thread-safe POD world for Adaptive candidate screening. Captured once on the
// main thread from live GD hitboxes / player fields; workers never touch Cocos.
class EbCpuPhysicsBody {
public:
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    // 0 solid/slope (land / block), 1 hazard (die), 2 pad (boost), 3 orb (boost if holding).
    std::uint8_t kind = 0;
    // Pad/orb strength hint: 0 yellow, 1 pink, 2 red, 3 gravity, 4 other.
    std::uint8_t boost = 0;

private:
};

class EbCpuPhysicsPlayer {
public:
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    float yVel = 0.f;
    float gravity = 0.958972f;
    float gravityMod = 1.f;
    float playerSpeed = 1.f;
    float vehicleSize = 1.f;
    bool upsideDown = false;
    bool grounded = false;
    bool holdingJump = false;
    // Mode bits mirror StageConfig GameModeKind.
    int mode = 0; // Cube=0 … Swing=7
    bool continuous = false;
    bool tapRelease = false;

private:
};

class EbCpuPhysicsWorld {
public:
    EbCpuPhysicsPlayer player;
    std::vector<EbCpuPhysicsBody> bodies;
    float floorY = 0.f;
    float ceilY = 9999.f;
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
