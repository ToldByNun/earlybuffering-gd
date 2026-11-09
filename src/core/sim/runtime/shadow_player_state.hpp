#ifndef SHADOW_PLAYER_STATE_HPP
#define SHADOW_PLAYER_STATE_HPP

#include "../shadow_simulator.hpp"

class EbShadowPlayerState {
public:
    static void copyWaveTrail(EbShadowSimulator& simulator, PlayerObject* destination,
                              PlayerObject* source);
    static void clearCollisionLogs(PlayerObject* state);
    static void copyPlayerState(PlayerObject* destination, PlayerObject* source);
    static size_t diagCaptureTransformCount(EbShadowSimulator& simulator, PlayLayer* layer);
    static int captureStartSnapshot(EbShadowSimulator& simulator, PlayerObject* real);
    static int captureSnapshot(EbShadowSimulator& simulator, PlayerObject* real);
    static int captureParitySnapshot(EbShadowSimulator& simulator, PlayerObject* real, int which);
    static PlayerObject* snapshotSlot(EbShadowSimulator const& simulator, int slot);

private:
    EbShadowPlayerState() = delete;
};

#endif
