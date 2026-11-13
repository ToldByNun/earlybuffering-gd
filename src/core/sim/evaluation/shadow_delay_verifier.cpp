#include "shadow_delay_verifier.hpp"
#include "../planning/shadow_survival_rules.hpp"
#include "../planning/simulation_plan.hpp"
#include "../shadow_simulator.hpp"
#include "../sim_session_guard.hpp"
#include <algorithm>
#include <chrono>

EbShadowSimulator::VerifyDelayResult
EbShadowDelayVerifier::verifyDelay(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                                 bool down, int delay, int limit, float deltaTime, float firstDt,
                                 EbPlanOptions const& options) {
    EbShadowSimulator::VerifyDelayResult out;
    if (!layer || !real || !simulator.m_shadow || limit < 0 || deltaTime <= 0.f)
        return out;
    if (firstDt <= 0.f || firstDt > deltaTime)
        firstDt = deltaTime;
    delay = std::max(0, delay);

    EbShadowSimulator::Model model = simulator.modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    int tickBudget =
        options.tickBudget > 0 ? options.tickBudget : EbShadowSimulator::kDefaultTickBudget;
    int const tickBudgetStart = tickBudget;
    auto const wall0 = std::chrono::steady_clock::now();
    simulator.beginPlanWall(wall0, options.budgetUs);
    auto wallExceeded = [&]() -> bool { return simulator.markPlanWallIfExceeded(); };
    auto fillSpent = [&]() {
        out.ticksSpent = std::max(0, tickBudgetStart - tickBudget);
        out.usSpent = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - wall0)
                                           .count());
    };

    auto linkedDual = [&]() -> bool {
        if (!layer || !layer->m_player2 || !real)
            return false;
        if (!layer->m_gameState.m_isDualMode)
            return false;
        if (auto* gm = GameManager::sharedState()) {
            if (gm->getGameVariable("0010"))
                return false;
        }
        return real == layer->m_player1 || real == layer->m_player2;
    };

    simulator.m_simTickBase = std::max(0, options.attemptStartTick);
    simulator.m_simAbsTick = -1;
    EbSimSessionGuard session(*(&simulator), layer);

    EbThreatZone zone{};
    // Probe d0 to build the same threat zone Live uses. When delay==0 the
    // planned candidate is identical — reuse that runOne (no second sim).
    auto c0 = simulator.runOne(layer, real, down, 0, limit, deltaTime, firstDt, model, -1, nullptr,
                               &tickBudget);
    simulator.resetCaptureForNextRun(layer);
    if (c0.died) {
        zone = simulator.buildThreatZone(layer, c0.deathX, c0.deathObject);
        out.threatZone = zone;
    }
    if (tickBudget <= 0 || wallExceeded()) {
        out.budgetHit = true;
        fillSpent();
        return out;
    }

    EbShadowSimulator::RunResult r;
    if (delay == 0) {
        r = c0;
    } else {
        r = simulator.runOne(layer, real, down, delay, limit, deltaTime, firstDt, model, -1,
                             zone.valid ? &zone : nullptr, &tickBudget);
        simulator.resetCaptureForNextRun(layer);
    }

    out.diedBeforeAction = r.diedBeforeAction;
    out.clearsThreat = EbShadowSurvivalRules::clearsThreatOf(r, zone);
    out.unknown = r.unknown || r.unknownTrunc || r.toggleUnknown;
    // clearsThreat honesty + ModeSurvival (soft or Alive-only via options).
    bool const requireAlive = options.requireAliveForDelay;
    bool viable = EbShadowSurvivalRules::runResultDelaySafe(r, zone, model, real, requireAlive);

    // Linked dual: still one runOne for the other player (even when delay==0).
    if (viable && linkedDual()) {
        PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
        if (other && tickBudget > 0 && !wallExceeded()) {
            auto r2 = simulator.runOne(layer, other, down, delay, limit, deltaTime, firstDt, model,
                                       -1, zone.valid ? &zone : nullptr, &tickBudget);
            simulator.resetCaptureForNextRun(layer);
            if (!EbShadowSurvivalRules::runResultDelaySafe(r2, zone, model, other, requireAlive)) {
                viable = false;
                out.clearsThreat = false;
            }
        }
    }

    if (tickBudget <= 0 || wallExceeded())
        out.budgetHit = true;

    fillSpent();
    // Budget exhaustion → do not trust bake (caller falls through / Pass).
    out.ok = viable && !out.budgetHit;
    return out;
}
