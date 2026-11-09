#ifndef ORB_ALIGN_HPP
#define ORB_ALIGN_HPP

#include <string>

// Commit 13 — Orb align / recheck / robust-arm helpers (pure; no PlayLayer).
// Early press waits inside orb window; wrong-orb reject; robust ±1 before arm;
// recheck late slide ≤+1 (never earlier).
class EbOrbAlign {
public:
    // Recheck may slide fire later by at most this many ticks (orb/toggle + non-orb).
    static constexpr int kRecheckMaxSlide = 1;

    // Clamp absolute fire into [press+dMin, press+dMax] ∩ (−∞, press+limit].
    // dMin/dMax < 0 → ignore that bound. Returns clamped fire (may equal input).
    static int clampFireIntoOrbWindow(int fireTick, int pressTick, int dMin, int dMax, int limit);

    // Early press: fire must not precede orb window start.
    static int waitUntilInsideRing(int fireTick, int pressTick, int dMin);

    // Activation delay outside the intended [dMin,dMax] → wrong-orb / reject.
    // Missing bounds (dMin/dMax < 0) → never reject.
    static bool wrongOrbReject(int actDelay, int dMin, int dMax);

    // Robust ±1 vor Arm: center must clear; each existing neighbor must clear.
    static bool robustNeighborsOk(bool centerOk, bool hasLo, bool loOk, bool hasHi, bool hiOk);

    // Recheck late-slide cap (Commit 13: ≤+1). Never earlier than oldFire.
    // Returns accepted fire or -1. Orb dMin wait is a separate path (caller).
    static int acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire, int absMax,
                                  int robustK, int deadline, bool planOk);

    static bool selfCheck(std::string& report);

private:
    EbOrbAlign() = delete;
};

#endif
