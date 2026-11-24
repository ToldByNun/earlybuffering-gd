#ifndef CPU_PHYSICS_ADAPTIVE_HPP
#define CPU_PHYSICS_ADAPTIVE_HPP

#include "../planning/search/adaptive/adaptive_candidate_slot.hpp"
#include "cpu_physics_types.hpp"
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// Adaptive Phase A/B screen via CPU Multithread POD physics.
// Survivors must still pass Phase C Shadow verify before arming.
class EbCpuPhysicsAdaptive {
public:
    class ScreenOutcome {
    public:
        bool used = false;
        bool parallel = false;
        bool fellBack = false;
        int workers = 1;
        int screened = 0;
        int survivors = 0;
        int us = 0;

    private:
    };

    // Returns true when slots were filled from the MT screen and Adaptive should
    // skip the serial Shadow Phase A/B loop (still runs Phase C Shadow verify).
    // False → caller must use the normal Shadow screen path.
    static bool tryScreen(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                          float deltaTime, float firstDt, int quantumTicks, int releaseAfter,
                          std::vector<EbAdaptiveCandidateSlot>& slots, ScreenOutcome& outcome);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsAdaptive() = delete;
};

#endif
