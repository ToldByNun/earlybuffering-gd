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
    double const signedError =
        (axisX * (y - target.y) - axisY * (x - target.x)) / target.bodySize;
    double const error = std::abs(signedError);
    double const travel = target.sideways ? dy * forward : dx * forward;
    double const transverse = target.sideways ? dx : dy;
    double const angle = std::atan2(transverse, travel) * 180. / std::numbers::pi;
    double const headingError = std::abs(std::remainder(angle - target.degrees, 360.));
    metrics.errorSum += error;
    metrics.maxError = std::max(metrics.maxError, error);
    metrics.lastError = error;
    metrics.lastSignedError = signedError;
    metrics.angleSum += headingError / 45.;
    metrics.lastHeadingError = std::remainder(angle - target.degrees, 360.);
    ++metrics.samples;
}
double EbShipLinePolicy::signedCrossTrack(Target const& target, double x, double y) {
    if (!target.enabled || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(target.x) ||
        !std::isfinite(target.y) || !std::isfinite(target.degrees) ||
        !std::isfinite(target.bodySize) || target.bodySize <= 0.)
        return std::numeric_limits<double>::quiet_NaN();
    double const radians = target.degrees * std::numbers::pi / 180.;
    double const forward = target.reversed ? -1. : 1.;
    double const axisX = target.sideways ? std::sin(radians) : forward * std::cos(radians);
    double const axisY = target.sideways ? forward * std::cos(radians) : std::sin(radians);
    return (axisX * (y - target.y) - axisY * (x - target.x)) / target.bodySize;
}
double EbShipLinePolicy::normalizedCrossTrack(Target const& target, double x, double y) {
    double const signedError = signedCrossTrack(target, x, y);
    return std::isfinite(signedError) ? std::abs(signedError)
                                      : std::numeric_limits<double>::infinity();
}
void EbShipLinePolicy::projectOntoLine(Target const& target, double x, double y, double& outX,
                                       double& outY) {
    outX = x;
    outY = y;
    if (!target.enabled || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(target.x) ||
        !std::isfinite(target.y) || !std::isfinite(target.degrees))
        return;
    double const radians = target.degrees * std::numbers::pi / 180.;
    double const forward = target.reversed ? -1. : 1.;
    double const axisX = target.sideways ? std::sin(radians) : forward * std::cos(radians);
    double const axisY = target.sideways ? forward * std::cos(radians) : std::sin(radians);
    double const along = axisX * (x - target.x) + axisY * (y - target.y);
    outX = target.x + axisX * along;
    outY = target.y + axisY * along;
}
bool EbShipLinePolicy::delayHelpsTowardLine(bool pressEdge, double signedCross,
                                            bool invertedControl) {
    if (!std::isfinite(signedCross))
        return false;
    if (invertedControl)
        pressEdge = !pressEdge;
    // Press delayed → fall more first. Helps only when currently above the line.
    // Release delayed → climb more first. Helps only when currently below the line.
    if (pressEdge)
        return signedCross > .05;
    return signedCross < -.05;
}
double EbShipLinePolicy::cost(Metrics const& metrics, int delay, int limit) {
    if (metrics.invalid || metrics.samples <= 0)
        return std::numeric_limits<double>::infinity();
    return metrics.errorSum / metrics.samples + .55 * metrics.maxError + .4 * metrics.lastError +
           .2 * metrics.angleSum / metrics.samples + .03 * std::max(0, delay) / std::max(1, limit);
}
bool EbShipLinePolicy::improves(Metrics const& baseline, Metrics const& candidate, int delay,
                                int limit) {
    if (delay <= 0 || delay > limit || baseline.samples != candidate.samples ||
        baseline.samples <= 0 || baseline.invalid || candidate.invalid)
        return false;
    double const original = cost(baseline, 0, limit);
    double const corrected = cost(candidate, delay, limit);
    if (!std::isfinite(original) || !std::isfinite(corrected) || corrected >= original)
        return false;
    if (candidate.maxError > baseline.maxError + .25)
        return false;
    if (candidate.lastError > baseline.lastError + .2)
        return false;
    if (std::abs(std::remainder(candidate.lastHeadingError - baseline.lastHeadingError, 360.)) > 55.)
        return false;
    if (baseline.maxError - candidate.maxError >= .02 ||
        baseline.lastError - candidate.lastError >= .02)
        return true;
    return original - corrected >= std::max(.004, original * .008);
}
