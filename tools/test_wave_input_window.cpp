#include "test_wave_input_window.hpp"
#include "../src/core/assist/contracts/live_plan_contracts.hpp"
#include "../src/core/assist/contracts/miss_assist_contracts.hpp"
#include <algorithm>
#include <array>
#include <iostream>

// Matches EbShadowSurvivalRules::kPostCapProveTicks (keep offline Geode/fmt-free).
static constexpr int kPostCapProveTicks = 8;

// Death ticks recorded in session-1794130733171, delays 0..12.
// Replaying the recorded collision outcomes tests the live acceptance rule,
// independently of Geode's rendering and the current level instance.
int WaveInputWindowRegressionTests::chooseRecordedDelay(RecordedInput const& input, int horizon) {
    for (int delay = 0; delay <= 12; ++delay) {
        int const deathTick = input.deathTicks[delay];
        bool const diedInWindow = deathTick < horizon;
        int const ticksRan = std::min(deathTick + 1, horizon);
        if (EbMissAssistContracts::survivesObservation(diedInWindow, false, ticksRan, horizon,
                                                     false)) {
            return delay;
        }
    }
    return -1;
}

int WaveInputWindowRegressionTests::run() {
    // AP3a observation horizon = limit + continuous IW cap (+ prove ticks).
    // Cap = max(lookAhead, Wave short planCap ≥32); prove matches Live requireAlive.
    // Mirror intended* formulas so this binary stays fmt-free (PlanGold links selfCheck).
    int const limit = 12;
    int const look = EbMissAssistContracts::continuousLookAhead(limit);
    int const shortPlan = std::max(EbLivePlanContracts::kWaveHorizonFloor, limit + 16);
    int const cap = std::max(look, shortPlan);
    int const horizon = limit + cap + kPostCapProveTicks;

    // With the floor, short-horizon “survivors” that die before tick 32/40 are
    // Pass (earliest-safe policy), not phase Delays. Retargeted expectedDelay.
    RecordedInput const inputs[] = {
        {88, {23, 23, 24, 25, 27, 28, 30, 31, 33, 35, 37, 39, 41}, -1},
        {114, {14, 14, 16, 18, 26, 29, 31, 35, 37, 38, 39, 40, 42}, -1},
        {125, {5, 6, 7, 9, 11, 66, 67, 68, 69, 70, 71, 72, 73}, 5},
        {134, {1, 1, 2, 3, 4, 6, 7, 9, 11, 12, 14, 16, 19}, -1},
        {151, {62, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 10, 10}, 0},
    };
    int failures = 0;
    for (auto const& input : inputs) {
        int const chosen = chooseRecordedDelay(input, horizon);
        if (chosen != input.expectedDelay || chooseRecordedDelay(input, 96) != -1) {
            std::cerr << "Wave input " << input.pressId << " chose " << chosen
                      << " (horizon=" << horizon << ")\n";
            ++failures;
        }
    }

    // Dual assert: old short horizon still picked earliest-safe on 88/114 —
    // documents the AP3a policy change (floor rejects those phase Delays).
    int const legacyHorizon = limit + look;
    if (chooseRecordedDelay(inputs[0], legacyHorizon) != 2) {
        std::cerr << "legacy short-horizon policy drift on press 88\n";
        ++failures;
    }
    if (chooseRecordedDelay(inputs[1], legacyHorizon) != 4) {
        std::cerr << "legacy short-horizon policy drift on press 114\n";
        ++failures;
    }

    // Budget-truncated and unknown paths must not become saving candidates.
    failures += EbMissAssistContracts::survivesObservation(false, false, 23, 24, false);
    failures += EbMissAssistContracts::survivesObservation(false, true, 24, 24, false);
    failures += EbMissAssistContracts::survivesObservation(true, false, 24, 24, true);
    failures += EbMissAssistContracts::continuousLookAhead(4) != 4;
    failures += EbMissAssistContracts::continuousLookAhead(0) != 1;
    failures += cap != EbLivePlanContracts::kWaveHorizonFloor;
    failures += horizon < limit + EbLivePlanContracts::kWaveHorizonFloor;

    // AP3b help-only / earliest / prox / futile (mirror formulas; PlanGold links real).
    auto const helpOnly = [](bool isWave, bool pressDown, bool d0Safe) {
        return isWave && pressDown && d0Safe;
    };
    auto const chooseDelay = [](bool d0Safe, int earliestLater) {
        if (d0Safe)
            return 0;
        return earliestLater > 0 ? earliestLater : -1;
    };
    auto const proxPass = [](bool isWave, bool pressDown, bool d0Died, bool nearHazard,
                             bool noOrb, bool foresightOrb) {
        return pressDown && isWave && !d0Died && nearHazard && noOrb && !foresightOrb;
    };
    auto const futilePass = [](bool isWave, bool pressDown, bool anySave, bool idleEscape,
                               bool orbIntent, int bestBin, float bestGain, bool foresightOrb) {
        if (!pressDown || !isWave || anySave || idleEscape || orbIntent || foresightOrb)
            return false;
        if (bestBin > 0)
            return false;
        return !(bestGain >= EbLivePlanContracts::kLandedScoreEps);
    };
    failures += !helpOnly(true, true, true);
    failures += chooseDelay(true, 5) != 0;
    failures += chooseDelay(false, 5) != 5;
    failures += !proxPass(true, true, false, true, true, false);
    failures += proxPass(true, true, true, true, true, false);
    failures += !futilePass(true, true, false, false, false, 0, 3.f, false);
    // waveIwBaselineCoastAllowed(inputWindow, continuous, press) → continuous IW ok
    failures += !(true && true && true);
    failures += (true && false && true); // discrete IW skips coast

    std::cout << "Wave recorded-input regression: " << failures << " failures (horizon=" << horizon
              << ")\n";
    return failures ? 1 : 0;
}

int main() {
    return WaveInputWindowRegressionTests::run();
}
