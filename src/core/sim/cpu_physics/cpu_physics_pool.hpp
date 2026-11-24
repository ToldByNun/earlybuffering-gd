#ifndef CPU_PHYSICS_POOL_HPP
#define CPU_PHYSICS_POOL_HPP

#include "cpu_physics_types.hpp"
#include <string>
#include <vector>

// Parallel delay screening over a frozen world (hardware_concurrency workers).
class EbCpuPhysicsPool {
public:
    class ScreenRequest {
    public:
        EbCpuPhysicsWorld const* world = nullptr;
        bool down = true;
        int limit = 0;
        int quantumTicks = 24;
        float deltaTime = 1.f / 240.f;
        float firstDt = 1.f / 240.f;
        int releaseAfter = -1;

    private:
    };

    class ScreenBatch {
    public:
        std::vector<EbCpuPhysicsRunResult> byDelay;
        int workers = 1;
        bool usedParallel = false;
        bool ok = false;

    private:
    };

    static unsigned workerCount();
    static ScreenBatch screenDelays(ScreenRequest const& request);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsPool() = delete;
};

#endif
