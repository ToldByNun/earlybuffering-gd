#include "wave_pattern_policy.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

double EbWavePatternPolicy::expectedSeconds(Target const& target) {
    return target.closingHold ? target.holdSeconds : target.releaseSeconds;
}

double EbWavePatternPolicy::phaseError(double completedSeconds, double expectedSeconds) {
    if (!(completedSeconds > 0.) || !(expectedSeconds > 0.))
        return 0.;
    return completedSeconds - expectedSeconds;
}

int EbWavePatternPolicy::suggestedDelayTicks(Target const& target, float deltaTime, int limit) {
    if (!target.enabled || !(deltaTime > 1e-6) || limit < 1)
        return 0;
    double const expected = expectedSeconds(target);
    if (!(expected > 0.))
        return 0;
    // Late or near-on-time: do not invent a past edge.
    if (!(target.phaseErrorSeconds < -target.minimumEarlySeconds))
        return 0;
    double correct = -target.phaseErrorSeconds * std::clamp(target.gain, 0., 1.5);
    correct = std::min(correct, target.maximumDelaySeconds);
    // Never stretch a segment past ~twice the expected duration via delay alone.
    correct = std::min(correct, expected * .9);
    if (!(correct >= target.minimumEarlySeconds))
        return 0;
    int ticks = static_cast<int>(std::lround(correct / deltaTime));
    return std::clamp(ticks, 1, limit);
}

double EbWavePatternPolicy::snapDegrees(double degrees, int stepDegrees) {
    if (stepDegrees <= 0 || !std::isfinite(degrees))
        return degrees;
    double const step = static_cast<double>(stepDegrees);
    // Bias half-steps toward zero so ±5° with step 10 stays 0 (flat line).
    double const quantized = degrees / step;
    double const rounded =
        std::copysign(std::floor(std::abs(quantized) + .5 - 1e-9), quantized);
    return rounded * step;
}

double EbWavePatternPolicy::snapDegreesSticky(double degrees, int stepDegrees,
                                              double lockedDegrees) {
    if (stepDegrees <= 0 || !std::isfinite(degrees))
        return degrees;
    double const step = static_cast<double>(stepDegrees);
    double const locked =
        std::isfinite(lockedDegrees) ? snapDegrees(lockedDegrees, stepDegrees) : snapDegrees(degrees, stepDegrees);
    double const delta = degrees - locked;
    // Stay on the locked bin until past ~70% of the way to a neighbor.
    if (std::abs(delta) < step * .7)
        return locked;
    return snapDegrees(degrees, stepDegrees);
}

double EbWavePatternPolicy::dutyFromTravelDegrees(double degrees) {
    if (!std::isfinite(degrees))
        return .5;
    double const radians = std::clamp(degrees, -40., 40.) * std::numbers::pi / 180.;
    double const slope = std::tan(radians);
    return std::clamp(.5 * (1. + slope), .08, .92);
}

double EbWavePatternPolicy::travelDegreesFromDuty(double duty) {
    if (!std::isfinite(duty))
        return 0.;
    double const clamped = std::clamp(duty, .08, .92);
    return std::atan(2. * clamped - 1.) * 180. / std::numbers::pi;
}

void EbWavePatternPolicy::holdReleaseForTravel(double travelDegrees, double periodSeconds,
                                               double& holdSeconds, double& releaseSeconds) {
    if (!(periodSeconds > 1e-4))
        return;
    double const duty = dutyFromTravelDegrees(travelDegrees);
    holdSeconds = duty * periodSeconds;
    releaseSeconds = periodSeconds - holdSeconds;
}

bool EbWavePatternPolicy::selfCheck(std::string& report) {
    report.clear();
    int failures = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++failures;
        if (!report.empty())
            report += "; ";
        report += label;
    };

    expect(std::abs(phaseError(.05, .10) + .05) < 1e-9, "early-error");
    expect(std::abs(phaseError(.12, .10) - .02) < 1e-9, "late-error");

    Target early;
    early.enabled = true;
    early.closingHold = true;
    early.holdSeconds = .10;
    early.releaseSeconds = .05;
    early.phaseErrorSeconds = -.04; // 40ms early hold close
    early.gain = 1.;
    early.maximumDelaySeconds = .085;
    expect(suggestedDelayTicks(early, .01f, 20) == 4, "early-delay-4");

    Target late = early;
    late.phaseErrorSeconds = .03;
    expect(suggestedDelayTicks(late, .01f, 20) == 0, "late-no-delay");

    Target tiny = early;
    tiny.phaseErrorSeconds = -.004;
    expect(suggestedDelayTicks(tiny, .01f, 20) == 0, "below-min-early");

    Target releaseEdge = early;
    releaseEdge.closingHold = false;
    releaseEdge.phaseErrorSeconds = -.03;
    expect(suggestedDelayTicks(releaseEdge, .01f, 20) == 3, "release-early-3");

    expect(std::abs(snapDegrees(14., 10) - 10.) < 1e-9, "snap-14-to-10");
    expect(std::abs(snapDegrees(16., 10) - 20.) < 1e-9, "snap-16-to-20");
    expect(std::abs(snapDegrees(5., 10)) < 1e-9, "snap-5-to-0");
    expect(std::abs(snapDegrees(-5., 10)) < 1e-9, "snap-m5-to-0");
    expect(std::abs(snapDegreesSticky(6., 10, 0.)) < 1e-9, "sticky-keep-0");
    expect(std::abs(snapDegreesSticky(8., 10, 0.) - 10.) < 1e-9, "sticky-to-10");
    expect(std::abs(snapDegrees(7., 0) - 7.) < 1e-9, "snap-off");
    expect(std::abs(dutyFromTravelDegrees(0.) - .5) < 1e-6, "duty-flat");
    double hold = 0.;
    double release = 0.;
    holdReleaseForTravel(0., .1, hold, release);
    expect(std::abs(hold - .05) < 1e-6 && std::abs(release - .05) < 1e-6, "flat-hold-release");
    holdReleaseForTravel(travelDegreesFromDuty(.667), .15, hold, release);
    expect(std::abs(hold / (hold + release) - .667) < .02, "duty-roundtrip");

    if (failures == 0) {
        report = "wavePatternPolicy OK";
        return true;
    }
    report = "wavePatternPolicy " + std::to_string(failures) + " fails: " + report;
    return false;
}
