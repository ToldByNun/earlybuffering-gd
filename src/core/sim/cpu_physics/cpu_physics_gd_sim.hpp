#ifndef CPU_PHYSICS_GD_SIM_HPP
#define CPU_PHYSICS_GD_SIM_HPP

#include "cpu_physics_types.hpp"
#include <string>

class PlayerObject;

// 1:1 GD step for one delay: PlayerObject::update + collidedWithObjectInternal
// against the frozen body list (object locks). Main thread must have seeded the
// ghost's mode/body via copyPlayerState before the parallel batch.
class EbCpuPhysicsGdSim {
public:
    class Options {
    public:
        bool down = true;
        int delay = 0;
        int maxTicks = 48;
        float deltaTime = 1.f / 240.f;
        float firstDt = 1.f / 240.f;
        int releaseAfter = -1;

    private:
    };

    // templateGhost: main-thread copyPlayerState snapshot (read-only during MT).
    static EbCpuPhysicsRunResult run(PlayerObject* ghost, PlayerObject* templateGhost,
                                     EbCpuPhysicsWorld const& world, Options const& options);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsGdSim() = delete;
};

#endif
