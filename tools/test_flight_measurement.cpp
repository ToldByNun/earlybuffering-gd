#include "test_flight_measurement.hpp"
#include "../src/core/assist/flight/flight_measurement.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
int FlightMeasurementRegression::run() {
    int failures = 0;
    int cases = 0;
    for (double dt : {1. / 60., 1. / 240., 1. / 1000.}) {
        for (int angle = -175; angle <= 175; angle += 5) {
            if (angle == -90 || angle == 90)
                continue;
            EbFlightMeasurement tracker;
            EbFlightMeasurement::Sample sample;
            sample.dt = dt;
            tracker.observe(sample);
            double const radians = angle * std::numbers::pi / 180.;
            for (int tick = 0; tick < 5; ++tick) {
                sample.x += std::cos(radians) * 1000. * dt;
                sample.y += std::sin(radians) * 1000. * dt;
                tracker.observe(sample);
            }
            auto const result = tracker.result();
            failures += !result.headingValid;
            failures +=
                std::abs(EbFlightMeasurement::angleDifference(result.worldAngle, angle)) > 1e-7;
            sample.inverted = true;
            tracker.observe(sample);
            failures += tracker.result().headingValid;
            sample.x += 10000.;
            tracker.observe(sample);
            failures += tracker.result().headingValid;
            sample.dt = std::numeric_limits<double>::quiet_NaN();
            tracker.observe(sample);
            failures += tracker.result().status != EbFlightMeasurement::Status::Invalid;
            ++cases;
        }
    }
    failures += std::abs(EbFlightMeasurement::angleDifference(-179., 179.) - 2.) > 1e-9;
    failures += std::abs(EbFlightMeasurement::angleDifference(179., -179.) + 2.) > 1e-9;
    EbFlightMeasurement tracker;
    EbFlightMeasurement::Sample sample;
    sample.dt = 1. / 240.;
    tracker.observe(sample);
    tracker.observe(sample);
    failures += tracker.result().headingValid;
    sample.sideways = true;
    tracker.observe(sample);
    sample.y += 4.;
    sample.x += 2.;
    tracker.observe(sample);
    failures += std::abs(tracker.result().travelAngle - 26.565051177) > 1e-6;
    // Context transitions cannot retain a heading from different physics.
    for (int transition = 0; transition < 8; ++transition) {
        EbFlightMeasurement measurement;
        EbFlightMeasurement::Sample context;
        context.dt = 1. / 240.;
        measurement.observe(context);
        context.x = 4.;
        measurement.observe(context);
        failures += !measurement.result().headingValid;
        switch (transition) {
        case 0:
            context.mode = 1;
            break;
        case 1:
            context.speed = 1.6;
            break;
        case 2:
            context.size = .6;
            break;
        case 3:
            context.timeWarp = .5;
            break;
        case 4:
            context.gravity = .8;
            break;
        case 5:
            context.inverted = true;
            break;
        case 6:
            context.sideways = true;
            break;
        case 7:
            context.reversed = true;
            break;
        }
        measurement.observe(context);
        failures += measurement.result().headingValid;
    }
    // Circular smoothing across the wrap must stay close to west.
    EbFlightMeasurement wrap;
    EbFlightMeasurement::Sample west;
    west.dt = 1. / 240.;
    wrap.observe(west);
    west.x = -4.;
    west.y = .05;
    wrap.observe(west);
    west.x -= 4.;
    west.y -= .05;
    wrap.observe(west);
    failures += std::abs(wrap.result().smoothedAngle) < 170.;
    std::cout << cases << " heading/transition sequences, " << failures << " failures\n";
    return failures ? 1 : 0;
}
int main() {
    return FlightMeasurementRegression::run();
}
