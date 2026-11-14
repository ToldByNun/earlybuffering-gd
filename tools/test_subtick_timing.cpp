#include "test_subtick_timing.hpp"
#include "../src/core/assist/contracts/subtick_timing.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

int SubtickTimingRegression::run() {
    int failures = 0;
    failures += !EbSubtickTiming::withinWindow(2, 0.5f, 12);
    failures += EbSubtickTiming::withinWindow(12, 0.25f, 12);
    failures += EbSubtickTiming::withinWindow(0, 0.5f, 12);
    failures += EbSubtickTiming::withinWindow(2, 1.f, 12);
    failures += EbSubtickTiming::withinWindow(2, std::numeric_limits<float>::quiet_NaN(), 12);
    failures += EbSubtickTiming::segmentPrefix(0.f, 1.f, 0.f, 1.f) >= 0.f;
    failures += EbSubtickTiming::segmentPrefix(0.5f, 1.f, 0.f, 0.2f) >= 0.f;
    failures += std::fabs(EbSubtickTiming::segmentPrefix(0.5f, 1.f, 0.2f, 0.4f) - 0.3f) > 0.00001f;
    failures += EbSubtickTiming::segmentPrefix(0.5f, 1.f, 0.6f, 0.4f) != 0.f;
    failures += EbSubtickTiming::segmentPrefix(0.5f, 0.f, 0.f, 1.f) >= 0.f;
    // Export 082715: failed presses 55/103 exhausted the wall before completing
    // d5/d4. Their idle deaths identify those candidates without scanning d1..3.
    failures += EbSubtickTiming::ufoBoundaryDelay(5, 12) != 5;
    failures += EbSubtickTiming::ufoBoundaryDelay(4, 12) != 4;
    // Successful presses 41/58 retain access to phases in d1/d2.
    failures += EbSubtickTiming::ufoBoundaryDelay(1, 12) != 1;
    failures += EbSubtickTiming::ufoBoundaryDelay(2, 12) != 2;
    failures += EbSubtickTiming::ufoBoundaryDelay(0, 12) != -1;
    failures += EbSubtickTiming::ufoBoundaryDelay(-1, 12) != -1;
    failures += EbSubtickTiming::ufoBoundaryDelay(4, 0) != -1;
    failures += EbSubtickTiming::ufoBoundaryDelay(13, 12) != 12;
    // Synthetic 0.6-tick window missed by whole ticks and covered by a midpoint.
    failures += !(2.5f >= 2.2f && 2.5f <= 2.8f);
    failures += 2.f >= 2.2f && 2.f <= 2.8f;
    failures += 3.f >= 2.2f && 3.f <= 2.8f;
    // CBF can provide several segments of one outer tick. Inject exactly once
    // and conserve the total physics delta across the split.
    std::array const segments{0.2f, 0.4f, 0.4f};
    float elapsed = 0.f;
    float appliedTime = 0.f;
    int injections = 0;
    for (float segment : segments) {
        float const prefix =
            injections == 0 ? EbSubtickTiming::segmentPrefix(0.5f, 1.f, elapsed, segment) : -1.f;
        if (prefix >= 0.f) {
            appliedTime += prefix;
            failures += std::fabs(appliedTime - 0.5f) > 0.00001f;
            ++injections;
            appliedTime += segment - prefix;
        } else {
            appliedTime += segment;
        }
        elapsed += segment;
    }
    failures += injections != 1 || std::fabs(appliedTime - 1.f) > 0.00001f;
    for (int index = 0; index < 3; ++index)
        failures += !EbSubtickTiming::withinWindow(2, EbSubtickTiming::probePhase(index), 12);
    std::cout << "Subtick timing regression: " << failures << " failures" << '\n';
    return failures ? 1 : 0;
}

int main() {
    return SubtickTimingRegression::run();
}
