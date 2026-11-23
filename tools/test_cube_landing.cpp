#include "test_cube_landing.hpp"
#include "../src/core/sim/planning/cube_landing_tracker.hpp"
#include <iostream>

int CubeLandingRegression::run() {
    int failures = 0;
    int cases = 0;
    // Initial contact cannot certify a jump. Cover short hops, the minimum
    // airtime, long jumps, interrupted contacts, and invalidated trajectories.
    for (int airborne = 0; airborne <= 12; ++airborne) {
        for (int contact = 0; contact <= 8; ++contact) {
            for (int interruption = 0; interruption < 3; ++interruption) {
                EbCubeLandingTracker tracker;
                for (int tick = 0; tick < 5; ++tick)
                    failures += tracker.observe(true, true);
                for (int tick = 0; tick < airborne; ++tick)
                    failures += tracker.observe(true, false);
                if (interruption == 1) {
                    tracker.observe(true, true);
                    tracker.observe(true, false);
                }
                if (interruption == 2)
                    failures += tracker.observe(false, true);
                bool landed = false;
                for (int tick = 0; tick < contact; ++tick)
                    landed = tracker.observe(true, true);
                int const effectiveAir =
                    interruption == 2
                        ? 0
                        : (interruption == 1 && airborne < 6 ? 1 : airborne + (interruption == 1));
                failures += landed != (effectiveAir >= 6 && contact >= 4);
                ++cases;
            }
        }
    }
    // Match Nova's 240-TPS grounding boundary without accepting a residual
    // starting contact, short hop, interrupted landing or invalidated jump.
    for (int airborne = 0; airborne <= 18; ++airborne) {
        for (int contact = 0; contact <= 8; ++contact) {
            EbCubeLandingTracker tracker;
            for (int tick = 0; tick < airborne; ++tick)
                failures += tracker.observe(true, false, 15, 5);
            bool landed = false;
            for (int tick = 0; tick < contact; ++tick)
                landed = tracker.observe(true, true, 15, 5);
            failures += landed != (airborne >= 15 && contact >= 5);
            ++cases;
        }
    }
    std::cout << cases << " landing sequences, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return CubeLandingRegression::run();
}
