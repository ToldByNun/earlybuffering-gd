#ifndef GAME_HOOK_SIMULATION_GUARD_HPP
#define GAME_HOOK_SIMULATION_GUARD_HPP

class EbGameHookSimulationGuard {
public:
    static bool simulating();

private:
    EbGameHookSimulationGuard() = delete;
};

#endif
