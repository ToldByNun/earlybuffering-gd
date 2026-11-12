#ifndef SHADOW_GHOST_LIFECYCLE_HPP
#define SHADOW_GHOST_LIFECYCLE_HPP

#include "../shadow_simulator.hpp"

class EbShadowGhostLifecycle {
public:
    static void attach(EbShadowSimulator& simulator, PlayLayer* layer);
    static void detach(EbShadowSimulator& simulator, bool layerDying);
    static bool isOwnedGhost(EbShadowSimulator const& simulator, PlayerObject* player);
    static void parkGhosts(EbShadowSimulator& simulator);
    static void beginSession(EbShadowSimulator& simulator, PlayLayer* layer);
    static void endSession(EbShadowSimulator& simulator, PlayLayer* layer);
    static void hideGhost(PlayerObject* player);

    // Drop streak points without HardStreak::reset / CCDrawNode::clear.
    // Safe only while the owning PlayerObject is still parented.
    static void clearWaveTrailGeometry(PlayerObject* player);

private:
    EbShadowGhostLifecycle() = delete;
};

#endif
