#ifndef CPU_PHYSICS_SEED_HPP
#define CPU_PHYSICS_SEED_HPP

#include "cpu_physics_types.hpp"
#include <string>

class PlayerObject;

class EbCpuPhysicsSeedIo {
public:
    static EbCpuPhysicsSeed capture(PlayerObject* source);
    // Compact POD restore (legacy / diagnostics). Prefer reseedFrom for GD runs.
    static void apply(PlayerObject* destination, EbCpuPhysicsSeed const& seed);
    // Thread-safe field copy from a main-thread copyPlayerState template.
    // Does not allocate trails, call switchedToMode, or touch ghostRoot.
    static void reseedFrom(PlayerObject* destination, PlayerObject* templateGhost);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsSeedIo() = delete;
};

#endif
