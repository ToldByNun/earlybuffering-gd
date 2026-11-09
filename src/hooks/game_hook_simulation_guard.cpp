#include "game_hook_simulation_guard.hpp"
#include "../core/sim/shadow_simulator.hpp"

bool EbGameHookSimulationGuard::simulating() {
    return EbShadowSimulator::get().isSimulating();
}
