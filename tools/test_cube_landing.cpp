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
    // Research 20261124_000327 contains Cube orb/gravity sequences with all
    // alternatives rejected at a later obstacle. Ground completion was disabled
    // solely by the ring flag, even for fully modeled, collision-checked flights.
    failures += !EbCubeLandingTracker::validAction(false, true, true, false, false, false, true);
    failures += !EbCubeLandingTracker::validAction(true, true, true, true, false, false, true);
    failures += EbCubeLandingTracker::validAction(false, false, true, false, false, false, true);
    failures += EbCubeLandingTracker::validAction(true, true, false, false, false, false, true);
    failures += EbCubeLandingTracker::validAction(true, false, true, false, false, false, true);
    failures += EbCubeLandingTracker::validAction(true, true, true, false, true, false, true);
    failures += EbCubeLandingTracker::validAction(true, true, true, false, false, true, true);
    failures += EbCubeLandingTracker::validAction(true, true, true, false, false, false, false);
    EbCubeLandingTracker orbLanding;
    bool const orbAction =
        EbCubeLandingTracker::validAction(false, true, true, false, false, false, true);
    // Orb contact alone is not success: require the whole airborne/contact proof.
    for (int tick = 0; tick < 10; ++tick)
        failures += orbLanding.observe(orbAction, true, 15, 5);
    for (int tick = 0; tick < 15; ++tick)
        failures += orbLanding.observe(orbAction, false, 15, 5);
    for (int tick = 0; tick < 4; ++tick)
        failures += orbLanding.observe(orbAction, true, 15, 5);
    failures += !orbLanding.observe(orbAction, true, 15, 5);
    std::cout << cases << " landing sequences, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return CubeLandingRegression::run();
}
