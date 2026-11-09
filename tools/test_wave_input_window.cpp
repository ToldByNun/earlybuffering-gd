#include "test_wave_input_window.hpp"
#include "../src/core/assist/contracts/miss_assist_contracts.hpp"
#include <algorithm>
#include <array>
#include <iostream>

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
    RecordedInput const inputs[] = {
        {88, {23, 23, 24, 25, 27, 28, 30, 31, 33, 35, 37, 39, 41}, 2},
        {114, {14, 14, 16, 18, 26, 29, 31, 35, 37, 38, 39, 40, 42}, 4},
        {125, {5, 6, 7, 9, 11, 66, 67, 68, 69, 70, 71, 72, 73}, 5},
        {134, {1, 1, 2, 3, 4, 6, 7, 9, 11, 12, 14, 16, 19}, -1},
        {151, {62, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 10, 10}, 0},
    };
    int failures = 0;
    int const horizon = 12 + EbMissAssistContracts::continuousLookAhead(12);
    for (auto const& input : inputs) {
        int const chosen = chooseRecordedDelay(input, horizon);
        if (chosen != input.expectedDelay || chooseRecordedDelay(input, 96) != -1) {
            std::cerr << "Wave input " << input.pressId << " chose " << chosen << '\n';
            ++failures;
        }
    }
    // Budget-truncated and unknown paths must not become saving candidates.
    failures += EbMissAssistContracts::survivesObservation(false, false, 23, 24, false);
    failures += EbMissAssistContracts::survivesObservation(false, true, 24, 24, false);
    failures += EbMissAssistContracts::survivesObservation(true, false, 24, 24, true);
    failures += EbMissAssistContracts::continuousLookAhead(4) != 4;
    failures += EbMissAssistContracts::continuousLookAhead(0) != 1;
    std::cout << "Wave recorded-input regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}

int main() {
    return WaveInputWindowRegressionTests::run();
}
