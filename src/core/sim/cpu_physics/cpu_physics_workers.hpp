#ifndef CPU_PHYSICS_WORKERS_HPP
#define CPU_PHYSICS_WORKERS_HPP

#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

// Main-thread-created PlayerObject ghosts for CPU Multithread GD steps.
// Each worker thread owns one ghost exclusively during a screen batch.
// A frozen template ghost holds the copyPlayerState snapshot for reseed.
class EbCpuPhysicsWorkers {
public:
    static unsigned workerCount();
    // Create/reuse ghosts under the Shadow ghost-root (main thread only).
    static bool ensure(PlayLayer* layer);
    static void clear();
    static std::vector<PlayerObject*> const& ghosts();
    static PlayerObject* templateGhost();
    // Full copyPlayerState from real onto template + every worker ghost.
    static void seedFrom(PlayLayer* layer, PlayerObject* real);

    static bool selfCheck(std::string& report);

private:
    EbCpuPhysicsWorkers() = delete;
};

#endif
