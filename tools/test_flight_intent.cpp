#include "test_flight_intent.hpp"
#include "../src/core/assist/flight/flight_intent.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

int FlightIntentRegression::run() {
    int failures = 0;
    int cases = 0;
    for (double angle : {-45., 0., 30.}) {
        for (double dt : {.005, .01, .02}) {
            for (bool assisted : {false, true}) {
                EbFlightIntent intent;
                EbFlightMeasurement::Result movement;
                movement.status = EbFlightMeasurement::Status::Valid;
                movement.headingValid = true;
                movement.travelAngle = movement.worldAngle = angle;
                movement.vx = std::cos(angle * std::numbers::pi / 180.) * 1000.;
                movement.vy = std::sin(angle * std::numbers::pi / 180.) * 1000.;
                int const interval = static_cast<int>(std::lround(.1 / dt));
                for (int tick = 0; tick < 10 * interval; ++tick) {
                    if (tick % interval == 0)
                        intent.rawEdge((tick / interval) % 2 == 0);
                    intent.observe(movement, dt, true, assisted);
                }
                failures += intent.result().targetValid == assisted;
                if (!assisted) {
                    failures += std::abs(intent.result().targetDegrees - angle) > 1e-6;
                    // Assisted trajectory cannot rotate a previously raw-supported target.
                    movement.travelAngle = 70.;
                    intent.observe(movement, dt, true, true);
                    failures += std::abs(intent.result().targetDegrees - angle) > 1e-6;
                    // Sustained input must invalidate target before reacquisition.
                    for (int tick = 0; tick < interval * 2; ++tick)
                        intent.observe(movement, dt, true, false);
                    failures += intent.result().targetValid;
                    // Invalid physics context cannot retain intent.
                    movement.status = EbFlightMeasurement::Status::Discontinuity;
                    intent.observe(movement, dt, true, false);
                    failures += intent.result().state != EbFlightIntent::State::Inactive;
                }
                ++cases;
            }
        }
    }
    for (bool manual : {false, true}) {
        EbFlightIntent intent;
        EbFlightIntent::Settings target;
        target.manualTarget = manual;
        target.targetDegrees = -20.;
        intent.configure(target);
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.vx = 1000.;
        for (int tick = 0; tick < 100; ++tick) {
            if (tick % 10 == 0)
                intent.rawEdge((tick / 10) % 2 == 0);
            intent.observe(movement, .01, true, false);
        }
        failures += !intent.result().targetValid;
        failures += std::abs(intent.result().targetDegrees - (manual ? -20. : 0.)) > 1e-6;
        // A single short interval must not replace a recognized target.
        intent.rawEdge(true);
        intent.observe(movement, .01, true, false);
        intent.rawEdge(false);
        failures += !intent.result().targetValid;
        // An explicit unsupported mode discards both target and cadence.
        intent.observe(movement, .01, false, false);
        failures += intent.result().targetValid;
        for (int tick = 0; tick < 100; ++tick)
            intent.observe(movement, .01, true, false);
        failures += intent.result().targetValid;
    }
    // Expired evidence cannot acquire a target on an unending old hold.
    EbFlightIntent stale;
    EbFlightMeasurement::Result movement;
    movement.status = EbFlightMeasurement::Status::Valid;
    movement.headingValid = true;
    movement.vx = 1000.;
    for (int tick = 0; tick < 100; ++tick) {
        if (tick < 50 && tick % 10 == 0)
            stale.rawEdge((tick / 10) % 2 == 0);
        stale.observe(movement, .01, true, false);
    }
    failures += stale.result().targetValid;
    EbFlightIntent invalid;
    EbFlightIntent::Settings settings;
    settings.acquisitionSeconds = std::numeric_limits<double>::quiet_NaN();
    settings.targetDegrees = std::numeric_limits<double>::infinity();
    invalid.configure(settings);
    EbFlightMeasurement::Result empty;
    invalid.observe(empty, std::numeric_limits<double>::quiet_NaN(), true, false);
    failures += invalid.result().targetValid;
    std::cout << cases << " cadence/angle/contamination sequences, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return FlightIntentRegression::run();
}
