#include "wave_pattern_policy.hpp"
#include <algorithm>
#include <cmath>
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

    if (failures == 0) {
        report = "wavePatternPolicy OK";
        return true;
    }
    report = "wavePatternPolicy " + std::to_string(failures) + " fails: " + report;
    return false;
}
