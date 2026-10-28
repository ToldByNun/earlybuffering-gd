#include "BakeVerifier.hpp"
#include "ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"

#include <algorithm>

BakeVerifier::Result BakeVerifier::verify(
    PlayLayer* layer, PlayerObject* player, bool down,
    int delayTicks, int limit, float dt, float firstDt
) {
    Result out;
    if (!layer || !player || limit < 0 || dt <= 0.f) return out;

    PlanOptions opts;
    opts.budgetUs = StageConfig::simBudgetUs();
    // One candidate + optional linked dual — keep a modest tick slice.
    int const full = StageConfig::tickBudgetPerPress();
    opts.tickBudget = full > 0 ? std::min(full, 1200) : 1200;

    auto const r = ShadowSimulator::get().verifyDelay(
        layer, player, down, std::max(0, delayTicks), limit, dt, firstDt, opts
    );
    out.ok = r.ok;
    out.clearsThreat = r.clearsThreat;
    out.diedBeforeAction = r.diedBeforeAction;
    out.budgetHit = r.budgetHit;
    return out;
}
