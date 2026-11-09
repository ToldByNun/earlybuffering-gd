#include "test_adaptive_parity_smoke.hpp"
#include "../src/core/assist/contracts/assist_verdict.hpp"
#include "../src/core/sim/planning/search/adaptive/adaptive_parity_policy.hpp"
#include <iostream>
#include <string>

// Offline Adaptive parity smoke (no Geode / no GD).
// Mirrors the Live fallthrough contract that blocked Assist when simUs=0
// emitted PassHopeless without classic fallthrough.
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

    if (!ok)
        ++fails;
    std::cout << "Adaptive parity smoke: " << fails << " failures\n";
    return fails ? 1 : 0;
}

int main() {
    return AdaptiveParitySmokeTests::run();
}
