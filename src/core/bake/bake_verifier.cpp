#include "bake_verifier.hpp"
#include "../../config/stage_config.hpp"
#include "../sim/shadow_simulator.hpp"
#include <algorithm>

EbBakeVerifier::Result EbBakeVerifier::verify(PlayLayer* layer, PlayerObject* player, bool down,
                                              int delayTicks, int limit, float deltaTime,
                                              float firstDt, EbPressSimBudget* shared,
                                              bool reserveForLive) {
    Result out;
    if (!layer || !player || limit < 0 || deltaTime <= 0.f)
        return out;

    EbPlanOptions opts;
    int const fullTick = EbStageConfig::tickBudgetPerPress();
    int const fullUs = EbStageConfig::simBudgetUs();
    // One candidate + optional linked dual — keep a modest tick slice.
    constexpr int kVerifyTickCap = 1200;

    if (shared) {
        int const tickDraw =
            std::min(std::max(0, shared->tickLeft - (reserveForLive ? kHybridLiveReserveTicks : 0)),
                     kVerifyTickCap);
        opts.tickBudget = tickDraw;
        if (shared->usUnlimited) {
            opts.budgetUs = 0; // user opted out of wall cap
        } else {
            int const usDraw =
                std::max(0, shared->usLeft - (reserveForLive ? kHybridLiveReserveUs : 0));
            // Never pass 0 into PlanOptions here — that means unlimited.
            opts.budgetUs = std::max(1, usDraw);
        }
    } else {
        opts.budgetUs = fullUs;
        opts.tickBudget = fullTick > 0 ? std::min(fullTick, kVerifyTickCap) : kVerifyTickCap;
    }

    auto const result = EbShadowSimulator::get().verifyDelay(
        layer, player, down, std::max(0, delayTicks), limit, deltaTime, firstDt, opts);
    out.ok = result.ok;
    out.clearsThreat = result.clearsThreat;
    out.diedBeforeAction = result.diedBeforeAction;
    out.budgetHit = result.budgetHit;
    out.ticksSpent = result.ticksSpent;
    out.usSpent = result.usSpent;
    out.threatZone = result.threatZone;

    if (shared) {
        shared->tickLeft = std::max(0, shared->tickLeft - result.ticksSpent);
        if (!shared->usUnlimited) {
            shared->usLeft = std::max(0, shared->usLeft - result.usSpent);
        }
    }
    return out;
}
