#ifndef CPU_PHYSICS_WORLD_HPP
#define CPU_PHYSICS_WORLD_HPP

#include "cpu_physics_types.hpp"
#include <string>

class PlayLayer;
class PlayerObject;

class EbCpuPhysicsWorldCapture {
public:
    class Options {
    public:
        float aheadX = 450.f;
        float behindX = 450.f;
        int horizonTicks = 48;
        int baseAbsTick = 0;
        float simDt = 1.f / 240.f;

    private:
    };

    // Freeze seed + nearby GameObject bodies (with TW AABB keyframes). Main thread.
    static EbCpuPhysicsWorld capture(PlayLayer* layer, PlayerObject* real, Options const& options);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsWorldCapture() = delete;
};

#endif
