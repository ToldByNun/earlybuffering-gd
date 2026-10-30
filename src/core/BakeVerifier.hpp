#ifndef EARLYBUFFER_CORE_BAKEVERIFIER_HPP
#define EARLYBUFFER_CORE_BAKEVERIFIER_HPP

#include "ShadowSimulator.hpp"

class PlayLayer;
class PlayerObject;

// Shared wall/tick remainder for one Hybrid press: bake verify draws first
// (leaving a Live reserve), Live fallthrough uses what is left.
// usLeft==0 with usUnlimited=false means exhausted — NOT PlanOptions unlimited.
struct PressSimBudget {
    int tickLeft = 0;
    int usLeft = 0;
    bool usUnlimited = false; // true only when user set sim-budget-us=0
};

// Verify must leave this much for Hybrid Live fallthrough (wall/tick).
constexpr int kHybridLiveReserveUs = 500;
constexpr int kHybridLiveReserveTicks = 800;

// One-press Hybrid fallthrough reuse: copy window fields (no bake-vector ptrs)
// + verify threat zone. Cleared each press; null/empty on missing-bake path.
struct HybridPressReuse {
    bool hasWindow = false;
    int earliest = 0;      // bake frames
    int latest = 0;
    int nominalFrame = 0;
    double bakeTps = 240.0;
    ThreatZone verifyThreat{};
};

// Live re-check of a BakeMap delay before Hybrid/Replay/Brute uses it.
// One runOne: must clearThreat and not die before the action.
class BakeVerifier {
public:
    struct Result {
        bool ok = false;
        bool clearsThreat = false;
        bool diedBeforeAction = false;
        bool budgetHit = false;
        int ticksSpent = 0;
        int usSpent = 0;
        ThreatZone threatZone{};
    };

    // delayTicks: 0 = fire now (already inside bake window).
    // shared non-null (Hybrid): draw from shared (verify tick slice still ≤1200)
    // and deduct spent. nullptr (Replay/Brute): independent caps as before.
    static Result verify(
        PlayLayer* layer, PlayerObject* player, bool down,
        int delayTicks, int limit, float dt, float firstDt,
        PressSimBudget* shared = nullptr
    );
};

#endif // EARLYBUFFER_CORE_BAKEVERIFIER_HPP
