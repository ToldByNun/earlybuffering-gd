#ifndef CPU_PHYSICS_WORLD_HPP
#define CPU_PHYSICS_WORLD_HPP

#include "cpu_physics_types.hpp"
#include <string>

class PlayLayer;
class PlayerObject;

class EbCpuPhysicsWorldCapture {
public:
    // Freeze player kinematics + nearby Include solids/hazards/pads/orbs (main thread).
    // aheadX / yReach bound the capture window; empty → invalid world.
    static EbCpuPhysicsWorld capture(PlayLayer* layer, PlayerObject* real, float aheadX,
                                     float yReach, float behindX = 40.f);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsWorldCapture() = delete;
};

#endif
