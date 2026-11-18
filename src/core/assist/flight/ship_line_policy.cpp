#include "ship_line_policy.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
void EbShipLinePolicy::observe(Target const& target, Metrics& metrics, double x, double y,
                               double dx, double dy) {
    if (!target.enabled)
        return;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(target.degrees) || !std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(target.bodySize) || target.bodySize <= 0.) {
        metrics.invalid = true;
        return;
    }
    double const radians = target.degrees * std::numbers::pi / 180.;
    double const forward = target.reversed ? -1. : 1.;
    double const axisX = target.sideways ? std::sin(radians) : forward * std::cos(radians);
    double const axisY = target.sideways ? forward * std::cos(radians) : std::sin(radians);
    double const error =
        std::abs(axisX * (y - target.y) - axisY * (x - target.x)) / target.bodySize;
    double const travel = target.sideways ? dy * forward : dx * forward;
    double const transverse = target.sideways ? dx : dy;
    double const angle = std::atan2(transverse, travel) * 180. / std::numbers::pi;
    double const headingError = std::abs(std::remainder(angle - target.degrees, 360.));
    metrics.errorSum += error;
    metrics.maxError = std::max(metrics.maxError, error);
    metrics.angleSum += headingError / 45.;
    metrics.lastHeadingError = std::remainder(angle - target.degrees, 360.);
    ++metrics.samples;
}
double EbShipLinePolicy::cost(Metrics const& metrics, int delay, int limit) {
    if (metrics.invalid || metrics.samples <= 0)
        return std::numeric_limits<double>::infinity();
    return metrics.errorSum / metrics.samples + .25 * metrics.maxError +
           .35 * metrics.angleSum / metrics.samples + .05 * std::max(0, delay) / std::max(1, limit);
}
bool EbShipLinePolicy::improves(Metrics const& baseline, Metrics const& candidate, int delay,
                                int limit) {
    if (delay <= 0 || delay > limit || baseline.samples != candidate.samples ||
        baseline.samples <= 0 || baseline.invalid || candidate.invalid)
        return false;
    double const original = cost(baseline, 0, limit);
    double const corrected = cost(candidate, delay, limit);
    // Playtest calibration: still reject tiny / unstable turns, but allow more
    // frequent local line corrections than the first AP05 defaults.
    return std::isfinite(original) && std::isfinite(corrected) &&
           candidate.maxError <= baseline.maxError + .08 &&
           std::abs(std::remainder(candidate.lastHeadingError - baseline.lastHeadingError, 360.)) <=
               30. &&
           candidate.angleSum / candidate.samples <= baseline.angleSum / baseline.samples + .15 &&
           original - corrected >= std::max(.04, original * .05);
}
