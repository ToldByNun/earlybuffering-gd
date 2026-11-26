#ifndef CPU_PHYSICS_SIM_HPP
#define CPU_PHYSICS_SIM_HPP

#include "cpu_physics_gd_sim.hpp"
#include <string>

// Compatibility alias — Adaptive MT screen uses EbCpuPhysicsGdSim (real GD update).
class EbCpuPhysicsSim {
public:
    using Options = EbCpuPhysicsGdSim::Options;

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsSim() = delete;
};

#endif
