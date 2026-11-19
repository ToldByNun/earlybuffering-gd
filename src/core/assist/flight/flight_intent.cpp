#include "flight_intent.hpp"
#include "flight_measurement.hpp"
#include "wave_pattern_policy.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

void EbFlightIntent::configure(Settings const& settings) {
    m_settings = settings;
    auto clamp = [](double value, double low, double high, double fallback) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    m_settings.acquisitionSeconds = clamp(settings.acquisitionSeconds, .1, 3., .25);
    m_settings.residualSeconds = clamp(settings.residualSeconds, .05, 1., .12);
    m_settings.durationTolerance = clamp(settings.durationTolerance, .1, 2., .75);
    m_settings.minimumDeviationSeconds = clamp(settings.minimumDeviationSeconds, .01, .25, .04);
    m_settings.maximumTargetAge = clamp(settings.maximumTargetAge, .5, 10., 8.);
    m_settings.targetDegrees = clamp(settings.targetDegrees, -89., 89., 0.);
    m_settings.angleSnapDegrees = std::clamp(settings.angleSnapDegrees, 0, 45);
    if (m_settings.manualTarget)
        m_settings.targetDegrees =
            EbWavePatternPolicy::snapDegrees(m_settings.targetDegrees, m_settings.angleSnapDegrees);
    reset();
}

void EbFlightIntent::setAngleSnapDegrees(int stepDegrees) {
    int const snap = std::clamp(stepDegrees, 0, 45);
    if (snap == m_settings.angleSnapDegrees)
        return;
    m_settings.angleSnapDegrees = snap;
    if (m_result.state == State::Tracking && m_result.targetValid) {
        if (m_settings.manualTarget)
            lockTargetDegrees(m_settings.targetDegrees);
        else
            lockTargetDegrees(m_result.targetDegrees);
    }
}

void EbFlightIntent::clearAcquisition() {
    m_windowSeconds = m_sumX = m_sumY = 0.;
}

void EbFlightIntent::reset() {
    m_result = {};
    m_time = m_edgeTime = m_lastEvidence = m_targetTime = m_yieldTime = 0.;
    m_hold = m_release = m_cleanAge = m_offHeadingSeconds = m_headingShiftSeconds = 0.;
    m_probeSumX = m_probeSumY = m_probeSeconds = 0.;
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

void EbFlightIntent::lockTargetDegrees(double measuredDegrees) {
    double degrees = measuredDegrees;
    if (m_settings.manualTarget)
        degrees = m_settings.targetDegrees;
    m_result.targetDegrees =
        std::clamp(EbWavePatternPolicy::snapDegrees(degrees, m_settings.angleSnapDegrees), -80.,
                   80.);
    m_headingShiftSeconds = 0.;
    m_probeSumX = m_probeSumY = m_probeSeconds = 0.;
}

void EbFlightIntent::yield(Reason reason) {
    m_result.state = State::Yielding;
    m_result.reason = reason;
    m_result.targetValid = false;
    m_result.yieldRequested = true;
    m_result.confidence = 0.;
    m_yieldTime = m_time;
    m_cleanAge = 0.;
    m_offHeadingSeconds = 0.;
    m_headingShiftSeconds = 0.;
    m_probeSumX = m_probeSumY = m_probeSeconds = 0.;
    m_result.cleanSeconds = 0.;
    clearAcquisition();
}

void EbFlightIntent::abandon(Reason reason) {
    yield(reason);
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
                if (m_deviations >= 3)
                    yield(Reason::RhythmChanged);
            } else {
                m_deviations = 0;
                mean = count == 0 ? duration : mean + .2 * (duration - mean);
                count = std::min(100, count + 1);
                m_lastEvidence = m_time;
                if (m_result.state == State::Tracking)
                    m_result.confidence = std::min(1., m_result.confidence + .06);
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
        m_offHeadingSeconds = 0.;
        m_headingShiftSeconds = 0.;
        m_probeSumX = m_probeSumY = m_probeSeconds = 0.;
    }
    double const expected = m_down ? m_hold : m_release;
    int const count = m_down ? m_holdCount : m_releaseCount;
    double const tolerance =
        std::max(m_settings.minimumDeviationSeconds, expected * m_settings.durationTolerance);
    double const sustainedLimit =
        expected + std::max(tolerance * 2., m_settings.minimumDeviationSeconds * 3.);
    if (m_result.state == State::Tracking && m_hasEdge && count >= 2 &&
        m_time - m_edgeTime > sustainedLimit) {
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
        // Instant travelAngle swings ±30° while clicking — never abandon on that.
        // Corridor exit is handled by cross-track in the controller.
        if (measurement.headingValid && !assisted && !m_settings.manualTarget) {
            double const radians = measurement.travelAngle * std::numbers::pi / 180.;
            double const speed = std::max(1., std::hypot(measurement.vx, measurement.vy));
            double const weight = speed * dt;
            double const decay = std::exp(-dt / .28);
            m_probeSumX = m_probeSumX * decay + std::cos(radians) * weight;
            m_probeSumY = m_probeSumY * decay + std::sin(radians) * weight;
            m_probeSeconds = std::min(1., m_probeSeconds + dt);
            double const probed =
                m_probeSeconds >= .08
                    ? std::atan2(m_probeSumY, m_probeSumX) * 180. / std::numbers::pi
                    : measurement.travelAngle;

            int const snap = m_settings.angleSnapDegrees;
            if (snap <= 0) {
                double const headingError =
                    EbFlightMeasurement::angleDifference(probed, m_result.targetDegrees);
                if (std::abs(headingError) <= 12.) {
                    // Locked line stays put — confirm only, no jitter rewrite.
                    m_lastEvidence = m_time;
                    m_result.confidence = std::min(1., m_result.confidence + dt * .15);
                    m_headingShiftSeconds = 0.;
                }
            } else {
                double const candidate = EbWavePatternPolicy::snapDegreesSticky(
                    probed, snap, m_result.targetDegrees);
                double const delta = std::abs(
                    EbFlightMeasurement::angleDifference(candidate, m_result.targetDegrees));
                if (delta < .5) {
                    m_headingShiftSeconds = 0.;
                    m_lastEvidence = m_time;
                    m_result.confidence = std::min(1., m_result.confidence + dt * .1);
                } else if (m_probeSeconds >= .12) {
                    m_headingShiftSeconds += dt;
                    if (m_headingShiftSeconds >= .35)
                        lockTargetDegrees(candidate);
                } else {
                    m_headingShiftSeconds = std::max(0., m_headingShiftSeconds - dt);
                }
            }
        }
        m_result.confidence = std::max(0., m_result.confidence - dt * .02);
        m_result.rawEvidenceAge = m_time - m_lastEvidence;
        if (m_result.rawEvidenceAge > 2. || m_time - m_targetTime > m_settings.maximumTargetAge)
            yield(Reason::StaleEvidence);
        return;
    }
    if (!measurement.headingValid || !std::isfinite(measurement.vx) ||
        !std::isfinite(measurement.vy) || m_cleanAge < m_settings.residualSeconds) {
        clearAcquisition();
        return;
    }
    m_windowSeconds += dt;
    double const radians = measurement.travelAngle * std::numbers::pi / 180.;
    double const speed = std::hypot(measurement.vx, measurement.vy);
    // Mild recent bias so multi-click acquisition settles on the flown line.
    double const decay = std::exp(-dt / .35);
    m_sumX = m_sumX * decay + std::cos(radians) * speed * dt;
    m_sumY = m_sumY * decay + std::sin(radians) * speed * dt;
    bool const cadence = m_holdCount >= 2 && m_releaseCount >= 2;
    double const target = std::atan2(m_sumY, m_sumX) * 180. / std::numbers::pi;
    // Flat / near-snap straight lines often use a long hold — allow lock from stable
    // heading without a full click cadence (still needs some flight evidence).
    double const flatBand =
        m_settings.angleSnapDegrees > 0
            ? std::max(6., m_settings.angleSnapDegrees * .55)
            : 10.;
    bool const steadyFlat = m_windowSeconds >= m_settings.acquisitionSeconds * 1.15 &&
                            std::abs(target) <= flatBand &&
                            (m_holdCount + m_releaseCount >= 1 || m_hasEdge);
    bool const fresh =
        m_hasEdge && m_time - m_edgeTime <= expected + tolerance && m_result.rawEvidenceAge <= .75;
    bool const freshEnough = cadence ? fresh : (m_result.rawEvidenceAge <= 1.5 || !m_hasEdge);
    if ((!cadence && !steadyFlat) || !freshEnough ||
        m_windowSeconds < m_settings.acquisitionSeconds) {
        if (m_windowSeconds > m_settings.acquisitionSeconds * 2.)
            clearAcquisition();
        return;
    }
    if (!m_settings.manualTarget && std::abs(target) > 80.) {
        clearAcquisition();
        return;
    }
    lockTargetDegrees(target);
    m_result.targetValid = true;
    m_result.confidence = .6;
    m_result.state = State::Tracking;
    m_result.reason = Reason::None;
    m_result.yieldRequested = false;
    m_offHeadingSeconds = 0.;
    m_targetTime = m_lastEvidence = m_time;
    clearAcquisition();
}
