#include "test_ball_landing.hpp"
#include "../src/core/assist/contracts/live_plan_contracts.hpp"
#include "../src/core/sim/planning/ball_landing_tracker.hpp"
#include <iostream>

int BallLandingRegression::run() {
    int failures = 0;
    failures += !EbLivePlanContracts::ballOrbFallbackAllowed(true, true, false, true);
    failures += EbLivePlanContracts::ballOrbFallbackAllowed(true, true, true, true);
    failures += EbLivePlanContracts::ballOrbFallbackAllowed(true, true, false, false);
    failures += EbLivePlanContracts::ballOrbFallbackAllowed(true, false, false, true);
    failures += EbLivePlanContracts::ballOrbFallbackAllowed(false, true, false, true);

    for (bool initialGravity : {false, true}) {
        EbBallLandingTracker landing;
        landing.reset(initialGravity);
        // Standing on the starting surface never certifies a flip.
        for (int tick = 0; tick < 12; ++tick)
            failures += landing.observe(initialGravity, true);
        // Portal/orb gravity change with residual ground flags is insufficient.
        failures += landing.observe(!initialGravity, true);
        failures += landing.observe(!initialGravity, false);
        for (int tick = 0; tick < 2; ++tick)
            failures += landing.observe(!initialGravity, true);
        // A brief touch followed by a gap restarts the stability window.
        failures += landing.observe(!initialGravity, false);
        for (int tick = 0; tick < 7; ++tick)
            failures += landing.observe(!initialGravity, true);
        // An Adaptive checkpoint carries the contact streak across quanta.
        auto checkpoint = landing;
        failures += !checkpoint.observe(!initialGravity, true);
        // Reversal onto the original gravity invalidates the landing.
        failures += landing.observe(initialGravity, true);
        for (int tick = 0; tick < 8; ++tick)
            failures += landing.observe(!initialGravity, true);
        landing.reset(initialGravity);
        failures += landing.observe(!initialGravity, false);
        for (int tick = 0; tick < 7; ++tick)
            failures += landing.observe(!initialGravity, true);
        failures += !landing.observe(!initialGravity, true);
    }
    std::cout << "Ball landing regression: " << failures << " failures\n";
    return failures ? 1 : 0;
}

int main() {
    return BallLandingRegression::run();
}
