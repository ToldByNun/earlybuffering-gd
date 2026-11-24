#ifndef CPU_PHYSICS_SIM_HPP
#define CPU_PHYSICS_SIM_HPP

#include "cpu_physics_types.hpp"
#include <string>

// Pure GD-like kinematics + AABB collision against a frozen world snapshot.
// No Cocos / PlayLayer / PlayerObject — safe on worker threads.
class EbCpuPhysicsSim {
public:
    class Options {
    public:
        bool down = true;
        int delay = 0;
        int maxTicks = 48;
        float deltaTime = 1.f / 240.f;
        float firstDt = 1.f / 240.f;
        int releaseAfter = -1; // ticks after press; <0 = hold / mode tap default

    private:
    };

    static EbCpuPhysicsRunResult run(EbCpuPhysicsWorld const& world, Options const& options);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsSim() = delete;
};

#endif
