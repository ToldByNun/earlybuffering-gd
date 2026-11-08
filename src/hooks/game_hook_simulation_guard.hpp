#ifndef GAME_HOOK_SIMULATION_GUARD_HPP
#define GAME_HOOK_SIMULATION_GUARD_HPP

class GameHookSimulationGuard {
public:
    static bool simulating();

private:
    GameHookSimulationGuard() = delete;
};

#endif
