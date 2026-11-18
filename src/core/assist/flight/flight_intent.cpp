#include "flight_intent.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
void EbFlightIntent::configure(Settings const& settings) {
    m_settings = settings;
    auto clamp = [](double value, double low, double high, double fallback) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    m_settings.acquisitionSeconds = clamp(settings.acquisitionSeconds, .1, 3., .3);
    m_settings.residualSeconds = clamp(settings.residualSeconds, .05, 1., .15);
    m_settings.durationTolerance = clamp(settings.durationTolerance, .1, 2., .5);
    m_settings.minimumDeviationSeconds = clamp(settings.minimumDeviationSeconds, .01, .25, .025);
    m_settings.maximumTargetAge = clamp(settings.maximumTargetAge, .5, 10., 3.);
    m_settings.targetDegrees = clamp(settings.targetDegrees, -89., 89., 0.);
    reset();
}
void EbFlightIntent::clearAcquisition() {
    m_windowSeconds = m_sumX = m_sumY = 0.;
}
void EbFlightIntent::reset() {
    m_result = {};
    m_time = m_edgeTime = m_lastEvidence = m_targetTime = m_yieldTime = 0.;
    m_hold = m_release = m_cleanAge = 0.;
    m_holdCount = m_releaseCount = m_deviations = 0;
    m_hasEdge = m_down = false;
    clearAcquisition();
}
EbFlightIntent::Result const& EbFlightIntent::result() const {
    return m_result;
}
char const* EbFlightIntent::stateName(State state) {
    switch (state) {
    case State::Inactive:
        return "Inactive";
    case State::Acquiring:
        return "Acquiring";
    case State::Tracking:
        return "Tracking";
    case State::Yielding:
        return "Yielding";
    case State::Reacquiring:
        return "Reacquiring";
    }
    return "Inactive";
}
void EbFlightIntent::yield(Reason reason) {
    m_result.state = State::Yielding;
    m_result.reason = reason;
    m_result.targetValid = false;
    m_result.yieldRequested = true;
    m_result.confidence = 0.;
    m_yieldTime = m_time;
    m_cleanAge = 0.;
    m_result.cleanSeconds = 0.;
    clearAcquisition();
}
void EbFlightIntent::rawEdge(bool down) {
    if (m_hasEdge && down == m_down)
        return;
    if (m_hasEdge) {
        double const duration = m_time - m_edgeTime;
        double& mean = m_down ? m_hold : m_release;
        int& count = m_down ? m_holdCount : m_releaseCount;
        if (duration > 0. && duration <= 2.) {
            double const tolerance =
                std::max(m_settings.minimumDeviationSeconds, mean * m_settings.durationTolerance);
            bool const deviation = count >= 2 && std::abs(duration - mean) > tolerance;
            if (m_result.state == State::Tracking && deviation) {
                ++m_deviations;
                if (m_deviations >= 2)
                    yield(Reason::RhythmChanged);
            } else {
                m_deviations = 0;
                mean = count == 0 ? duration : mean + .2 * (duration - mean);
                count = std::min(100, count + 1);
                m_lastEvidence = m_time;
                if (m_result.state == State::Tracking)
                    m_result.confidence = std::min(1., m_result.confidence + .04);
            }
        }
    }
    m_down = down;
    m_hasEdge = true;
    m_edgeTime = m_time;
}
void EbFlightIntent::observe(EbFlightMeasurement::Result const& measurement, double dt,
                             bool supported, bool assisted) {
    if (!supported || !std::isfinite(dt) || dt <= 0. || dt > .1 ||
        measurement.status == EbFlightMeasurement::Status::Invalid ||
        measurement.status == EbFlightMeasurement::Status::Discontinuity) {
        reset();
        m_result.reason = Reason::ContextChanged;
        return;
    }
    m_time += dt;
    m_result.rawEvidenceAge = m_time - m_lastEvidence;
    m_result.holdSeconds = m_hold;
    m_result.releaseSeconds = m_release;
    if (m_result.state == State::Inactive)
        m_result.state = State::Acquiring;
    if (m_result.state == State::Yielding) {
        if (m_time - m_yieldTime < .1)
            return;
        m_result.state = State::Reacquiring;
        m_result.yieldRequested = false;
        m_hold = m_release = 0.;
        m_holdCount = m_releaseCount = m_deviations = 0;
        m_edgeTime = m_time;
        m_lastEvidence = m_time;
    }
    double const expected = m_down ? m_hold : m_release;
    int const count = m_down ? m_holdCount : m_releaseCount;
    double const tolerance =
        std::max(m_settings.minimumDeviationSeconds, expected * m_settings.durationTolerance);
    if (m_result.state == State::Tracking && m_hasEdge && count >= 2 &&
        m_time - m_edgeTime > expected + tolerance) {
        yield(Reason::SustainedInput);
        return;
    }
    if (assisted) {
        m_cleanAge = 0.;
        clearAcquisition();
    } else {
        m_cleanAge += dt;
    }
    m_result.cleanSeconds = m_cleanAge;
    if (m_result.state == State::Tracking) {
        m_result.confidence = std::max(0., m_result.confidence - dt * .08);
        if (m_result.rawEvidenceAge > .75 || m_time - m_targetTime > m_settings.maximumTargetAge)
            yield(Reason::StaleEvidence);
        return;
    }
    if (!measurement.headingValid || !std::isfinite(measurement.vx) ||
        !std::isfinite(measurement.vy) || m_cleanAge < m_settings.residualSeconds) {
        clearAcquisition();
        return;
    }
    // Only uncontaminated observed movement can acquire an automatic line.
    // Raw completed cycles independently establish that the player is feathering.
    m_windowSeconds += dt;
    double const radians = measurement.travelAngle * std::numbers::pi / 180.;
    double const speed = std::hypot(measurement.vx, measurement.vy);
    m_sumX += std::cos(radians) * speed * dt;
    m_sumY += std::sin(radians) * speed * dt;
    bool const cadence = m_holdCount >= 2 && m_releaseCount >= 2;
    bool const fresh =
        m_hasEdge && m_time - m_edgeTime <= expected + tolerance && m_result.rawEvidenceAge <= .75;
    if (!cadence || !fresh || m_windowSeconds < m_settings.acquisitionSeconds) {
        if (m_windowSeconds > m_settings.acquisitionSeconds * 2.)
            clearAcquisition();
        return;
    }
    double const target = std::atan2(m_sumY, m_sumX) * 180. / std::numbers::pi;
    if (!m_settings.manualTarget && std::abs(target) > 80.) {
        clearAcquisition();
        return;
    }
    m_result.targetDegrees = m_settings.manualTarget ? m_settings.targetDegrees : target;
    m_result.targetValid = true;
    m_result.confidence = .6;
    m_result.state = State::Tracking;
    m_result.reason = Reason::None;
    m_result.yieldRequested = false;
    m_targetTime = m_lastEvidence = m_time;
}
