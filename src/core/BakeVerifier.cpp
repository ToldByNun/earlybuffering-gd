#include "BakeVerifier.hpp"
#include "ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"

#include <algorithm>

BakeVerifier::Result BakeVerifier::verify(
    PlayLayer* layer, PlayerObject* player, bool down,
    int delayTicks, int limit, float dt, float firstDt,
    PressSimBudget* shared
) {
    Result out;
    if (!layer || !player || limit < 0 || dt <= 0.f) return out;

    PlanOptions opts;
    int const fullTick = StageConfig::tickBudgetPerPress();
    int const fullUs = StageConfig::simBudgetUs();
    // One candidate + optional linked dual — keep a modest tick slice.
    constexpr int kVerifyTickCap = 1200;

    if (shared) {
        int const tickDraw = std::min(
            std::max(0, shared->tickLeft - kHybridLiveReserveTicks),
            kVerifyTickCap
        );
        opts.tickBudget = tickDraw;
        if (shared->usUnlimited) {
            opts.budgetUs = 0; // user opted out of wall cap
        }
        else {
            int const usDraw = std::max(0, shared->usLeft - kHybridLiveReserveUs);
            // Never pass 0 into PlanOptions here — that means unlimited.
            opts.budgetUs = std::max(1, usDraw);
        }
    }
    else {
        opts.budgetUs = fullUs;
        opts.tickBudget = fullTick > 0 ? std::min(fullTick, kVerifyTickCap) : kVerifyTickCap;
    }

    auto const r = ShadowSimulator::get().verifyDelay(
        layer, player, down, std::max(0, delayTicks), limit, dt, firstDt, opts
    );
    out.ok = r.ok;
    out.clearsThreat = r.clearsThreat;
    out.diedBeforeAction = r.diedBeforeAction;
    out.budgetHit = r.budgetHit;
    out.ticksSpent = r.ticksSpent;
    out.usSpent = r.usSpent;
    out.threatZone = r.threatZone;

    if (shared) {
        shared->tickLeft = std::max(0, shared->tickLeft - r.ticksSpent);
        if (!shared->usUnlimited) {
            shared->usLeft = std::max(0, shared->usLeft - r.usSpent);
        }
    }
    return out;
}
