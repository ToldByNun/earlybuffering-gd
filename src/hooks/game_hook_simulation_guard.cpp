#include "game_hook_simulation_guard.hpp"
#include "../core/sim/shadow_simulator.hpp"

bool GameHookSimulationGuard::simulating() {
    return ShadowSimulator::get().isSimulating();
}
