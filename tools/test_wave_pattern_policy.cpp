#include "test_wave_pattern_policy.hpp"
#include "../src/core/assist/flight/wave_pattern_policy.hpp"
#include <iostream>
#include <string>

int WavePatternPolicyRegression::run() {
    std::string report;
    int const failures = EbWavePatternPolicy::selfCheck(report) ? 0 : 1;
    std::cout << report << "\n";
    return failures ? 1 : 0;
}

int main() {
    return WavePatternPolicyRegression::run();
}
