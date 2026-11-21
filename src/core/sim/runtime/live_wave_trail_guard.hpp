#ifndef LIVE_WAVE_TRAIL_GUARD_HPP
#define LIVE_WAVE_TRAIL_GUARD_HPP

#include <Geode/Geode.hpp>

// Hard isolation for the real player's HardStreak.
// Shadow/snapshot/eval code may freely share trails among themselves; they must
// never mutate, clear, stop, or steal the protected live stroke.
class EbLiveWaveTrailGuard {
public:
    // Call once per live P1/P2 physics update so the active HardStreak is pinned.
    static void protectPlayer(PlayerObject* player);

    static void clear();

    static bool isProtected(HardStreak* trail);

    // True if this HardStreak belongs to a live P1/P2 right now.
    static bool isLivePlayerTrail(HardStreak* trail);

    // Return true when the caller must no-op (trail is the live stroke).
    static bool refuseMutation(HardStreak* trail);

private:
    EbLiveWaveTrailGuard() = delete;

    static HardStreak* s_protectedP1;
    static HardStreak* s_protectedP2;
};

#endif
