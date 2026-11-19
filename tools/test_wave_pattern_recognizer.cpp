#include "test_wave_pattern_recognizer.hpp"
#include "../src/core/assist/flight/wave_pattern_recognizer.hpp"
#include <iostream>
#include <string>

int WavePatternRegression::run() {
    std::string report;
    int const failures = EbWavePatternRecognizer::selfCheck(report) ? 0 : 1;
    std::cout << report << "\n";
    return failures ? 1 : 0;
}

int main() {
    return WavePatternRegression::run();
}
