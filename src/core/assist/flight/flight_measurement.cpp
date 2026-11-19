#include "flight_measurement.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

double EbFlightMeasurement::angleDifference(double angle, double reference) {
    return std::remainder(angle - reference, 360.);
}
bool EbFlightMeasurement::compatible(Sample const& left, Sample const& right) {
    return left.mode == right.mode && left.inverted == right.inverted &&
           left.sideways == right.sideways && left.reversed == right.reversed &&
           left.speed == right.speed && left.size == right.size &&
           left.timeWarp == right.timeWarp && left.gravity == right.gravity;
}
void EbFlightMeasurement::reset() {
    m_previous = {};
    m_result = {};
    m_hasPrevious = false;
    m_smoothX = m_smoothY = 0.;
}
EbFlightMeasurement::Result const& EbFlightMeasurement::result() const {
    return m_result;
}
void EbFlightMeasurement::observe(Sample const& sample) {
    bool const finite = std::isfinite(sample.x) && std::isfinite(sample.y) &&
                        std::isfinite(sample.dt) && std::isfinite(sample.speed) &&
                        std::isfinite(sample.size) && std::isfinite(sample.timeWarp) &&
                        std::isfinite(sample.gravity);
    if (!sample.valid || !finite || sample.dt <= 0. || sample.dt > .1 || sample.speed <= 0. ||
        sample.size <= 0. || sample.timeWarp <= 0.) {
        reset();
        m_result.status = Status::Invalid;
        return;
    }
    if (!m_hasPrevious || !compatible(sample, m_previous)) {
        bool const transition = m_hasPrevious;
        reset();
        m_previous = sample;
        m_hasPrevious = true;
        m_result.status = transition ? Status::Discontinuity : Status::Acquiring;
        return;
    }
    double const dx = sample.x - m_previous.x;
    double const dy = sample.y - m_previous.y;
    double const distance = std::hypot(dx, dy);
    // Native position jumps outside the plausible local physics envelope
    // invalidate heading. This is a discontinuity detector, not a collision test.
    double const maxDistance = std::max(32. * sample.size, 12000. * sample.speed * sample.dt);
    m_previous = sample;
    if (distance > maxDistance) {
        reset();
        m_previous = sample;
        m_hasPrevious = true;
        m_result.status = Status::Discontinuity;
        return;
    }
    double const vx = dx / sample.dt;
    double const vy = dy / sample.dt;
    double const speed = std::hypot(vx, vy);
    if (speed < 1. || std::abs(sample.sideways ? vy : vx) < 1.) {
        m_result = {};
        m_result.status = Status::Stationary;
        m_smoothX = m_smoothY = 0.;
        return;
    }
    bool const first = !m_result.headingValid;
    double const alpha = -std::expm1(-sample.dt / .04);
    double const errorX = vx - m_smoothX;
    double const errorY = vy - m_smoothY;
    m_result.velocityVariance =
        first ? 0.
              : (1. - alpha) *
                    (m_result.velocityVariance + alpha * (errorX * errorX + errorY * errorY));
    m_smoothX = first ? vx : m_smoothX + alpha * errorX;
    m_smoothY = first ? vy : m_smoothY + alpha * errorY;
    constexpr double degrees = 180. / std::numbers::pi;
    m_result.vx = vx;
    m_result.vy = vy;
    m_result.worldAngle = std::atan2(vy, vx) * degrees;
    // Travel-relative positive transverse motion is world up (or world right
    // in sideways mode). Gravity is reported separately and never mirrors world Y.
    double const forward = std::abs(sample.sideways ? vy : vx);
    double const transverse = sample.sideways ? vx : vy;
    m_result.travelAngle = std::atan2(transverse, forward) * degrees;
    m_result.smoothedAngle = std::atan2(m_smoothY, m_smoothX) * degrees;
    m_result.observedSeconds += sample.dt;
    m_result.headingValid = true;
    m_result.status = Status::Valid;
}
