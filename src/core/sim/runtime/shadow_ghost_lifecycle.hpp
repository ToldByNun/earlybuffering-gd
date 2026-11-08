#ifndef SHADOW_GHOST_LIFECYCLE_HPP
#define SHADOW_GHOST_LIFECYCLE_HPP

#include "../shadow_simulator.hpp"

class ShadowGhostLifecycle {
public:
    static void attach(ShadowSimulator& simulator, PlayLayer* layer);
    static void detach(ShadowSimulator& simulator, bool layerDying);
    static bool isOwnedGhost(ShadowSimulator const& simulator, PlayerObject* player);
    static void parkGhosts(ShadowSimulator& simulator);
    static void beginSession(ShadowSimulator& simulator, PlayLayer* layer);
    static void endSession(ShadowSimulator& simulator, PlayLayer* layer);
    static void hideGhost(PlayerObject* player);

private:
    ShadowGhostLifecycle() = delete;
};

#endif
