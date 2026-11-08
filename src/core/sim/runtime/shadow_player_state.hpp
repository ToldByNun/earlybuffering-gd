#ifndef SHADOW_PLAYER_STATE_HPP
#define SHADOW_PLAYER_STATE_HPP

#include "../shadow_simulator.hpp"

class ShadowPlayerState {
public:
    static void copyWaveTrail(ShadowSimulator& simulator, PlayerObject* destination,
                              PlayerObject* source);
    static void clearCollisionLogs(PlayerObject* state);
    static void copyPlayerState(PlayerObject* destination, PlayerObject* source);
    static size_t diagCaptureTransformCount(ShadowSimulator& simulator, PlayLayer* layer);
    static int captureStartSnapshot(ShadowSimulator& simulator, PlayerObject* real);
    static int captureSnapshot(ShadowSimulator& simulator, PlayerObject* real);
    static int captureParitySnapshot(ShadowSimulator& simulator, PlayerObject* real, int which);
    static PlayerObject* snapshotSlot(ShadowSimulator const& simulator, int slot);

private:
    ShadowPlayerState() = delete;
};

#endif
