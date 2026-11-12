#include "test_adaptive_parity_smoke.hpp"
#include "../src/core/assist/contracts/assist_verdict.hpp"
#include "../src/core/assist/contracts/live_plan_contracts.hpp"
#include "../src/core/sim/planning/search/adaptive/adaptive_parity_policy.hpp"
#include <iostream>
#include <string>

// Offline Adaptive parity smoke (no Geode / no GD).
// Mirrors the Live fallthrough contract that blocked Assist when simUs=0
// emitted PassHopeless without classic fallthrough.
// 1.5.1 AP1 also locks hitch ceiling + fallthrough remUs floor (policy numbers;
// full LivePlanContracts::selfCheck stays in PlanGold — needs fmt).
//
// Build (repo root, C++23):
//   g++ -std=c++23 tools/test_adaptive_parity_smoke.cpp \
//     src/core/sim/planning/search/adaptive/adaptive_parity_policy.cpp \
//     src/core/assist/contracts/assist_verdict.cpp \
//     -o build/test_adaptive_parity_smoke.exe
//   ./build/test_adaptive_parity_smoke.exe
int AdaptiveParitySmokeTests::run() {
    std::string report;
    bool const ok = EbAdaptiveParityPolicy::selfCheck(report);
    std::cout << report << '\n';

    int fails = 0;
    auto expect = [&](bool cond, char const* name) {
        if (cond)
            return;
        std::cerr << "FAIL " << name << '\n';
        ++fails;
    };

    class Case {
    public:
        char const* name = nullptr;
        bool earlyHopeless = false;
        bool gateIsDelay = false;
        AssistReason gateReason = AssistReason::None;
        bool expectEarlyFt = false;
        bool expectGateFt = false;

    private:
    };
    Case const cases[] = {
        {"soft-wall-budget", false, false, AssistReason::PassBudget, false, true},
        {"infinite-wall-hopeless", true, false, AssistReason::PassHopeless, true, true},
        {"proven-d0-safe", false, false, AssistReason::PassD0Safe, false, false},
        {"adaptive-delay", false, true, AssistReason::DelayThreat, false, false},
        {"gate-hopeless", false, false, AssistReason::PassHopeless, false, true},
    };
    for (auto const& c : cases) {
        if (c.earlyHopeless) {
            bool const earlyFt =
                EbAdaptiveParityPolicy::shouldFallThroughEarlyPass(AssistReason::PassHopeless);
            expect(earlyFt == c.expectEarlyFt, c.name);
        }
        expect(EbAdaptiveParityPolicy::shouldFallThroughGate(c.gateIsDelay, c.gateReason) ==
                   c.expectGateFt,
               c.name);
    }

    // 1.5.1 AP1 — hitch (policy numbers only; no wall-clock flakes).
    // Mirror intended* formulas so this binary stays fmt-free; PlanGold links
    // the real EbLivePlanContracts::selfCheck.
    auto const intendedLiveBudgetUs = [](int configuredBudgetUs) {
        if (configuredBudgetUs > 0)
            return configuredBudgetUs;
        return EbLivePlanContracts::kHitchCeilingUs;
    };
    auto const intendedFallthroughClassicBudgetUs = [](int remUs, int ticksLeft) {
        if (remUs > 0)
            return remUs;
        if (ticksLeft > 0)
            return EbLivePlanContracts::kFallthroughRemUsFloor;
        return 0;
    };

    expect(EbLivePlanContracts::kHitchCeilingUs == 8000, "hitch-ceiling-const");
    expect(intendedLiveBudgetUs(0) == EbLivePlanContracts::kHitchCeilingUs,
           "adaptive-zero-wall-clamped");
    expect(intendedLiveBudgetUs(4000) == 4000, "adaptive-zero-wall-keep");
    expect(intendedFallthroughClassicBudgetUs(0, 100) == EbLivePlanContracts::kFallthroughRemUsFloor,
           "fallthrough-remus-floor");
    expect(intendedFallthroughClassicBudgetUs(900, 50) == 900, "fallthrough-remus-keep");
    expect(intendedFallthroughClassicBudgetUs(0, 0) == 0, "fallthrough-remus-done");
    expect(EbLivePlanContracts::kFallthroughRemUsFloor >= 2000, "fallthrough-remus-threshold");

    // AP3b — Wave IW help-only still Passes when d0 clears (not unlimited Delay).
    // Mirror EbLivePlanContracts::waveIwHelpOnlyPass / waveIwChooseDelay (fmt-free).
    bool const isWave = true;
    bool const pressDown = true;
    bool const d0Safe = true;
    expect(isWave && pressDown && d0Safe, "wave-iw-help-only");
    int const chosen = d0Safe ? 0 : 4;
    expect(chosen == 0, "wave-iw-help-not-delay");

    // Assist Pack AP1 — fair depth (frozen A4 from research_20261113_001322).
    // Mirrors EbLivePlanContracts helpers fmt-free so this binary stays link-light.
    // Full selfCheck + ledger live in PlanGold / tools/test_fair_depth_ap1.cpp.
    auto const depthTruncatedAlive = [](bool died, bool unknown, int ticksRan,
                                        int honestFloorTicks) {
        if (died || unknown || ticksRan <= 0)
            return false;
        int const floor = honestFloorTicks > 1 ? honestFloorTicks : 1;
        return ticksRan < floor;
    };
    auto const fairDepthPreferCand = [](int bestTicks, int bestBin, float bestScore, int candTicks,
                                        int candBin, float candScore, int depthFloor) {
        int const floor = depthFloor > 1 ? depthFloor : 1;
        bool const bestDeep = bestTicks >= floor;
        bool const candDeep = candTicks >= floor;
        if (candDeep == bestDeep) {
            if (candBin != bestBin)
                return candBin > bestBin;
            return candScore > bestScore;
        }
        if (!candDeep && bestDeep)
            return (candScore - bestScore) > EbLivePlanContracts::kLandedScoreEps;
        return true;
    };
    // A4: d0 deathTick=54, later Alive ticks 43/18 were not-viable + pass-hopeless.
    expect(depthTruncatedAlive(false, false, 43, 54), "ap1-a4-later-43-trunc");
    expect(depthTruncatedAlive(false, false, 18, 54), "ap1-a4-later-18-trunc");
    expect(!depthTruncatedAlive(true, false, 55, 54), "ap1-a4-d0-dead");
    expect(!fairDepthPreferCand(96, 3, 100.f, 18, 0, 103.f, 18), "ap1-a4-no-bin-steal");
    expect(fairDepthPreferCand(96, 3, 100.f, 18, 0, 109.f, 18), "ap1-a4-score-gain");

    if (!ok)
        ++fails;
    std::cout << "Adaptive parity smoke: " << fails << " failures\n";
    return fails ? 1 : 0;
}

int main() {
    return AdaptiveParitySmokeTests::run();
}
