#include "wave_pattern_recognizer.hpp"
#include "wave_pattern_policy.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

void EbWavePatternRecognizer::configure(Settings const& settings) {
    m_settings = settings;
    auto clamp = [](double value, double low, double high, double fallback) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    m_settings.durationTolerance = clamp(settings.durationTolerance, .1, 1.5, .35);
    m_settings.minimumDeviationSeconds = clamp(settings.minimumDeviationSeconds, .005, .2, .012);
    m_settings.maximumSegmentSeconds = clamp(settings.maximumSegmentSeconds, .2, 3., 1.25);
    m_settings.maximumTargetAge = clamp(settings.maximumTargetAge, 1., 12., 6.);
    m_settings.residualSeconds = clamp(settings.residualSeconds, .02, .5, .08);
    m_settings.lockCycles = std::clamp(settings.lockCycles, 2, 8);
    m_settings.motifMaxPairs = std::clamp(settings.motifMaxPairs, 1, 2);
    m_settings.angleSnapDegrees = std::clamp(settings.angleSnapDegrees, 0, 45);
    reset();
}

void EbWavePatternRecognizer::setAngleSnapDegrees(int stepDegrees) {
    int const snap = std::clamp(stepDegrees, 0, 45);
    if (snap == m_settings.angleSnapDegrees)
        return;
    m_settings.angleSnapDegrees = snap;
    if (m_result.state == State::Tracking && m_travelLocked && m_result.periodSeconds > 0.)
        lockTravelTarget(m_result.targetTravelDegrees);
}

void EbWavePatternRecognizer::reset() {
    m_result = {};
    m_segmentCount = m_segmentWrite = 0;
    m_time = m_edgeTime = m_lastEvidence = m_targetTime = m_yieldTime = 0.;
    m_cleanAge = m_sumX = m_sumY = m_travelSeconds = 0.;
    m_probeSumX = m_probeSumY = m_probeSeconds = m_headingShiftSeconds = 0.;
    m_templateLen = m_matchedCycles = m_deviations = 0;
    m_hasEdge = m_down = m_edgeAssisted = m_travelLocked = false;
    for (double& value : m_template)
        value = 0.;
}

void EbWavePatternRecognizer::publishDurations(double holdSeconds, double releaseSeconds) {
    m_result.holdSeconds = holdSeconds;
    m_result.releaseSeconds = releaseSeconds;
    m_result.periodSeconds = holdSeconds + releaseSeconds;
    m_result.dutyCycle =
        m_result.periodSeconds > 1e-6 ? holdSeconds / m_result.periodSeconds : 0.;
}

void EbWavePatternRecognizer::lockTravelTarget(double measuredDegrees) {
    double measured = measuredDegrees;
    if (!std::isfinite(measured)) {
        measured = EbWavePatternPolicy::travelDegreesFromDuty(
            m_result.periodSeconds > 1e-6 ? m_result.holdSeconds / m_result.periodSeconds : .5);
    }
    double const snapped = EbWavePatternPolicy::snapDegrees(measured, m_settings.angleSnapDegrees);
    m_result.targetTravelDegrees = snapped;
    m_travelLocked = true;
    if (m_settings.angleSnapDegrees > 0 && m_result.periodSeconds > 1e-4) {
        double hold = m_result.holdSeconds;
        double release = m_result.releaseSeconds;
        EbWavePatternPolicy::holdReleaseForTravel(snapped, m_result.periodSeconds, hold, release);
        publishDurations(hold, release);
    }
    m_headingShiftSeconds = 0.;
    m_probeSumX = m_probeSumY = m_probeSeconds = 0.;
}

EbWavePatternRecognizer::Result const& EbWavePatternRecognizer::result() const {
    return m_result;
}

char const* EbWavePatternRecognizer::stateName(State state) {
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

void EbWavePatternRecognizer::yield(Reason reason) {
    m_result.state = State::Yielding;
    m_result.reason = reason;
    m_result.targetValid = false;
    m_result.yieldRequested = true;
    m_result.confidence = 0.;
    m_result.matchedCycles = 0;
    m_yieldTime = m_time;
    m_cleanAge = 0.;
    m_matchedCycles = 0;
    m_deviations = 0;
    m_sumX = m_sumY = m_travelSeconds = 0.;
    m_probeSumX = m_probeSumY = m_probeSeconds = m_headingShiftSeconds = 0.;
    m_travelLocked = false;
}

void EbWavePatternRecognizer::abandon(Reason reason) {
    yield(reason);
}

void EbWavePatternRecognizer::pushSegment(bool hold, double seconds, bool assisted) {
    if (!(seconds > 0. && seconds <= m_settings.maximumSegmentSeconds))
        return;
    Segment segment;
    segment.hold = hold;
    segment.seconds = seconds;
    segment.assisted = assisted;
    m_segments[m_segmentWrite] = segment;
    m_segmentWrite = (m_segmentWrite + 1) % kSegmentCapacity;
    m_segmentCount = std::min(kSegmentCapacity, m_segmentCount + 1);
    m_result.lastCompletedSeconds = seconds;
    m_result.lastCompletedHold = hold;
    m_result.hasLastCompleted = true;
    if (!assisted)
        m_lastEvidence = m_time;
}

void EbWavePatternRecognizer::rawEdge(bool down) {
    if (m_hasEdge && down == m_down)
        return;
    if (m_hasEdge) {
        double const duration = m_time - m_edgeTime;
        pushSegment(m_down, duration, m_edgeAssisted);
        if (!m_edgeAssisted)
            scoreMotifs();
    }
    m_down = down;
    m_hasEdge = true;
    m_edgeTime = m_time;
    m_edgeAssisted = false;
    m_result.holding = down;
}

void EbWavePatternRecognizer::updateTravel(EbFlightMeasurement::Result const& measurement,
                                           double dt, bool assisted) {
    if (assisted || !measurement.headingValid)
        return;
    double const radians = measurement.travelAngle * std::numbers::pi / 180.;
    double const speed = std::hypot(measurement.vx, measurement.vy);
    if (!(speed > 1.))
        return;
    double const weight = speed * dt;
    if (!m_travelLocked) {
        m_sumX += std::cos(radians) * weight;
        m_sumY += std::sin(radians) * weight;
        m_travelSeconds += dt;
        if (m_travelSeconds >= .05)
            m_result.averageTravelDegrees =
                std::atan2(m_sumY, m_sumX) * 180. / std::numbers::pi;
        return;
    }

    // Locked: keep a short probe of recent travel. Do not rewrite the assist
    // heading on ordinary click jitter — only accept a neighboring snap step
    // after sustained evidence (Ship-like stability).
    double const decay = std::exp(-dt / .28);
    m_probeSumX = m_probeSumX * decay + std::cos(radians) * weight;
    m_probeSumY = m_probeSumY * decay + std::sin(radians) * weight;
    m_probeSeconds = std::min(1., m_probeSeconds + dt);
    if (m_probeSeconds < .12)
        return;
    double const measured = std::atan2(m_probeSumY, m_probeSumX) * 180. / std::numbers::pi;
    m_result.averageTravelDegrees = measured;
    int const snap = m_settings.angleSnapDegrees;
    if (snap <= 0) {
        double const err = EbFlightMeasurement::angleDifference(measured, m_result.targetTravelDegrees);
        if (std::abs(err) <= 10.) {
            // Confirm lock; do not rewrite the assist heading on click jitter.
            m_headingShiftSeconds = 0.;
            m_lastEvidence = m_time;
            m_result.confidence = std::min(1., m_result.confidence + dt * .1);
        } else if (std::abs(err) >= 18.) {
            m_headingShiftSeconds += dt;
            if (m_headingShiftSeconds >= .45)
                lockTravelTarget(measured);
        } else {
            m_headingShiftSeconds = std::max(0., m_headingShiftSeconds - dt);
        }
        return;
    }

    double const candidate = EbWavePatternPolicy::snapDegreesSticky(
        measured, snap, m_result.targetTravelDegrees);
    double const delta =
        std::abs(EbFlightMeasurement::angleDifference(candidate, m_result.targetTravelDegrees));
    if (delta < .5) {
        m_headingShiftSeconds = 0.;
        m_lastEvidence = m_time;
        m_result.confidence = std::min(1., m_result.confidence + dt * .08);
        return;
    }
    m_headingShiftSeconds += dt;
    if (m_headingShiftSeconds >= .45)
        lockTravelTarget(candidate);
}

void EbWavePatternRecognizer::scoreMotifs() {
    if (m_segmentCount < 4)
        return;

    auto at = [&](int ageFromNewest) -> Segment const& {
        int index = m_segmentWrite - 1 - ageFromNewest;
        while (index < 0)
            index += kSegmentCapacity;
        return m_segments[index % kSegmentCapacity];
    };

    // Newest completed segment first. Require strict alternation.
    if (at(0).hold == at(1).hold)
        return;
    bool const newestHold = at(0).hold;

    class EbWaveMotifCandidate {
    public:
        int pairs = 1;
        int cycles = 0;
        double error = 0.;
        double jitter = 0.;
        double templateMeans[4]{};
        int templateLen = 0;
        double holdMean = 0.;
        double releaseMean = 0.;

    private:
    };
    EbWaveMotifCandidate best;
    best.error = 1e9;

    int const maxPairs = m_settings.motifMaxPairs;
    for (int pairs = 1; pairs <= maxPairs; ++pairs) {
        int const motifLen = pairs * 2;
        if (m_segmentCount < motifLen * m_settings.lockCycles)
            continue;
        EbWaveMotifCandidate candidate;
        candidate.pairs = pairs;
        candidate.templateLen = motifLen;
        int counts[4]{};
        double means[4]{};
        int const usable = std::min(m_segmentCount, motifLen * 6);
        bool polarityOk = true;
        for (int age = 0; age < usable; ++age) {
            auto const& segment = at(age);
            bool const expectHold = ((age % 2) == 0) ? newestHold : !newestHold;
            if (segment.hold != expectHold) {
                polarityOk = false;
                break;
            }
        }
        if (!polarityOk)
            continue;
        for (int age = 0; age < usable; ++age) {
            auto const& segment = at(age);
            if (segment.assisted)
                continue;
            int const slot = age % motifLen;
            means[slot] += segment.seconds;
            ++counts[slot];
        }
        bool ok = true;
        for (int slot = 0; slot < motifLen; ++slot) {
            if (counts[slot] < m_settings.lockCycles) {
                ok = false;
                break;
            }
            means[slot] /= counts[slot];
            candidate.templateMeans[slot] = means[slot];
        }
        if (!ok)
            continue;

        double error = 0.;
        double jitter = 0.;
        int samples = 0;
        int cycles = 0;
        for (int age = 0; age + motifLen <= usable; age += motifLen) {
            bool cycleOk = true;
            double cycleErr = 0.;
            for (int slot = 0; slot < motifLen; ++slot) {
                auto const& segment = at(age + slot);
                bool const expectHold = (((age + slot) % 2) == 0) ? newestHold : !newestHold;
                if (segment.assisted || segment.hold != expectHold) {
                    cycleOk = false;
                    break;
                }
                double const delta = segment.seconds - means[slot];
                double const tol =
                    std::max(m_settings.minimumDeviationSeconds,
                             means[slot] * m_settings.durationTolerance);
                if (std::abs(delta) > tol * 1.75) {
                    cycleOk = false;
                    break;
                }
                cycleErr += (delta * delta) / std::max(1e-6, means[slot] * means[slot]);
                jitter += std::abs(delta);
                ++samples;
            }
            if (!cycleOk)
                break;
            error += cycleErr;
            ++cycles;
        }
        if (cycles < m_settings.lockCycles || samples <= 0)
            continue;
        error = error / cycles + .08 * (pairs - 1); // complexity penalty
        jitter /= samples;
        candidate.cycles = cycles;
        candidate.error = error;
        candidate.jitter = jitter;

        double holdSum = 0.;
        double releaseSum = 0.;
        int holdN = 0;
        int releaseN = 0;
        for (int slot = 0; slot < motifLen; ++slot) {
            bool const hold = ((slot % 2) == 0) ? newestHold : !newestHold;
            if (hold) {
                holdSum += means[slot];
                ++holdN;
            } else {
                releaseSum += means[slot];
                ++releaseN;
            }
        }
        candidate.holdMean = holdN > 0 ? holdSum / holdN : 0.;
        candidate.releaseMean = releaseN > 0 ? releaseSum / releaseN : 0.;

        if (candidate.error + 1e-9 < best.error)
            best = candidate;
    }

    if (best.cycles < m_settings.lockCycles || !(best.error < 1e8))
        return;

    // Already Tracking: never wholesale-replace the locked motif from a noisy
    // recent window. Gentle confirmation only (Ship-like consistency).
    if (m_result.state == State::Tracking && m_result.targetValid) {
        refineLockedMotif();
        return;
    }

    m_templateLen = best.templateLen;
    for (int slot = 0; slot < best.templateLen; ++slot)
        m_template[slot] = best.templateMeans[slot];
    m_matchedCycles = best.cycles;
    m_result.motifPairs = best.pairs;
    m_result.matchedCycles = best.cycles;
    publishDurations(best.holdMean, best.releaseMean);
    m_result.jitterSeconds = best.jitter;
    m_result.targetValid = true;
    m_result.reason = Reason::None;
    m_result.yieldRequested = false;
    m_result.confidence = std::clamp(.45 + .12 * best.cycles - best.error, .2, 1.);
    m_result.state = State::Tracking;
    m_targetTime = m_lastEvidence = m_time;
    m_deviations = 0;
    double measuredTravel = m_result.averageTravelDegrees;
    if (!(m_travelSeconds >= .05))
        measuredTravel = EbWavePatternPolicy::travelDegreesFromDuty(m_result.dutyCycle);
    lockTravelTarget(measuredTravel);
}

void EbWavePatternRecognizer::refineLockedMotif() {
    if (m_segmentCount < 2 || m_templateLen < 2)
        return;
    auto const& newest =
        m_segments[(m_segmentWrite + kSegmentCapacity - 1) % kSegmentCapacity];
    if (newest.assisted)
        return;
    double const mean = newest.hold ? m_result.holdSeconds : m_result.releaseSeconds;
    if (!(mean > 0.))
        return;
    double const tol =
        std::max(m_settings.minimumDeviationSeconds, mean * m_settings.durationTolerance);
    double const delta = newest.seconds - mean;
    if (std::abs(delta) > tol * 1.25)
        return;
    // With degree snap the duty cycle is owned by the locked heading — only
    // confirm confidence. Without snap, gently blend durations (Ship-like).
    if (m_settings.angleSnapDegrees <= 0) {
        double const blended = mean + .06 * delta;
        if (newest.hold)
            publishDurations(blended, m_result.releaseSeconds);
        else
            publishDurations(m_result.holdSeconds, blended);
    }
    m_result.confidence = std::min(1., m_result.confidence + .03);
    m_targetTime = m_time;
}

void EbWavePatternRecognizer::observe(EbFlightMeasurement::Result const& measurement, double dt,
                                      bool supported, bool assisted) {
    if (!supported || !std::isfinite(dt) || dt <= 0. || dt > .1 ||
        measurement.status == EbFlightMeasurement::Status::Invalid ||
        measurement.status == EbFlightMeasurement::Status::Discontinuity) {
        reset();
        m_result.reason = Reason::ContextChanged;
        return;
    }
    m_time += dt;
    if (m_hasEdge && assisted)
        m_edgeAssisted = true;
    m_result.rawEvidenceAge = m_time - m_lastEvidence;
    if (m_result.state == State::Inactive)
        m_result.state = State::Acquiring;

    if (m_result.state == State::Yielding) {
        if (m_time - m_yieldTime < .12)
            return;
        m_result.state = State::Reacquiring;
        m_result.yieldRequested = false;
        m_segmentCount = m_segmentWrite = 0;
        m_matchedCycles = m_deviations = 0;
        m_templateLen = 0;
        m_edgeTime = m_time;
        m_lastEvidence = m_time;
        m_sumX = m_sumY = m_travelSeconds = 0.;
        m_probeSumX = m_probeSumY = m_probeSeconds = m_headingShiftSeconds = 0.;
        m_travelLocked = false;
    }

    if (assisted) {
        m_cleanAge = 0.;
    } else {
        m_cleanAge += dt;
    }

    // Phase within current open segment relative to expected mean.
    if (m_result.state == State::Tracking && m_hasEdge && m_result.periodSeconds > 0.) {
        double const expected = m_down ? m_result.holdSeconds : m_result.releaseSeconds;
        m_result.phaseSeconds = std::clamp(m_time - m_edgeTime, 0., expected + .5);
        double const sustainedLimit =
            expected + std::max(expected * m_settings.durationTolerance * 2.,
                                m_settings.minimumDeviationSeconds * 4.);
        if (m_time - m_edgeTime > sustainedLimit) {
            yield(Reason::SustainedInput);
            return;
        }
        if (!assisted && m_segmentCount >= 2) {
            // Fresh completed edge already scored in rawEdge; watch drift vs template.
            auto const& newest = m_segments[(m_segmentWrite + kSegmentCapacity - 1) %
                                            kSegmentCapacity];
            if (!newest.assisted && m_templateLen > 0) {
                double const mean = newest.hold ? m_result.holdSeconds : m_result.releaseSeconds;
                double const tol =
                    std::max(m_settings.minimumDeviationSeconds, mean * m_settings.durationTolerance);
                if (std::abs(newest.seconds - mean) > tol * 2.2) {
                    ++m_deviations;
                    // Require a clear run of bad edges before abandoning the locked line.
                    if (m_deviations >= 5) {
                        yield(Reason::RhythmChanged);
                        return;
                    }
                } else {
                    m_deviations = 0;
                    m_result.confidence = std::min(1., m_result.confidence + .04);
                }
            }
        }
        updateTravel(measurement, dt, assisted);
        // Soft decay only — locked motif must survive ordinary click jitter.
        m_result.confidence = std::max(.15, m_result.confidence - dt * .012);
        m_result.rawEvidenceAge = m_time - m_lastEvidence;
        if (m_result.rawEvidenceAge > 2.5 || m_time - m_targetTime > m_settings.maximumTargetAge) {
            yield(Reason::StaleEvidence);
            return;
        }
        return;
    }

    if (m_cleanAge < m_settings.residualSeconds)
        return;
    updateTravel(measurement, dt, assisted);
    if (m_result.state == State::Acquiring || m_result.state == State::Reacquiring)
        scoreMotifs();
}

bool EbWavePatternRecognizer::selfCheck(std::string& report) {
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

    // Symmetric 50/50 spam → Tracking with ~equal hold/release.
    {
        EbWavePatternRecognizer recognizer;
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.travelAngle = 0.;
        movement.vx = 800.;
        movement.vy = 0.;
        double const dt = .01;
        bool down = true;
        for (int tick = 0; tick < 240; ++tick) {
            if (tick % 5 == 0) {
                down = !down;
                recognizer.rawEdge(down);
            }
            recognizer.observe(movement, dt, true, false);
        }
        auto const& result = recognizer.result();
        expect(result.state == State::Tracking, "sym-tracking");
        expect(result.targetValid, "sym-valid");
        expect(std::abs(result.holdSeconds - .05) < .02, "sym-hold");
        expect(std::abs(result.releaseSeconds - .05) < .02, "sym-release");
        expect(std::abs(result.dutyCycle - .5) < .15, "sym-duty");
        expect(result.motifPairs == 1, "sym-motif1");
    }

    // Asymmetric 100ms hold / 50ms release.
    {
        EbWavePatternRecognizer recognizer;
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.vx = 800.;
        movement.vy = 200.;
        movement.travelAngle = 15.;
        double const dt = .01;
        double phase = 0.;
        bool down = false;
        for (int tick = 0; tick < 400; ++tick) {
            phase += dt;
            double const need = down ? .10 : .05;
            if (phase + 1e-9 >= need) {
                phase = 0.;
                down = !down;
                recognizer.rawEdge(down);
            }
            recognizer.observe(movement, dt, true, false);
        }
        auto const& result = recognizer.result();
        expect(result.state == State::Tracking, "asym-tracking");
        expect(std::abs(result.holdSeconds - .10) < .03, "asym-hold");
        expect(std::abs(result.releaseSeconds - .05) < .025, "asym-release");
        expect(result.dutyCycle > .55 && result.dutyCycle < .75, "asym-duty");
        expect(result.confidence > .4, "asym-conf");
    }

    // Assisted edges must not lock a fake pattern alone.
    {
        EbWavePatternRecognizer recognizer;
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.vx = 800.;
        bool down = true;
        for (int tick = 0; tick < 200; ++tick) {
            if (tick % 5 == 0) {
                down = !down;
                recognizer.rawEdge(down);
            }
            recognizer.observe(movement, .01, true, true);
        }
        expect(!recognizer.result().targetValid, "assisted-no-lock");
    }

    // Inverse 50/100 still locks with distinct ratio from 100/50.
    {
        EbWavePatternRecognizer recognizer;
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.vx = 800.;
        double phase = 0.;
        bool down = false;
        for (int tick = 0; tick < 400; ++tick) {
            phase += .01;
            double const need = down ? .05 : .10;
            if (phase + 1e-9 >= need) {
                phase = 0.;
                down = !down;
                recognizer.rawEdge(down);
            }
            recognizer.observe(movement, .01, true, false);
        }
        auto const& result = recognizer.result();
        expect(result.state == State::Tracking, "inv-tracking");
        expect(result.dutyCycle < .45, "inv-duty");
    }

    // Locked motif must stay put through a few messy edges (Ship-like).
    {
        EbWavePatternRecognizer recognizer;
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.travelAngle = 0.;
        movement.vx = 800.;
        movement.vy = 0.;
        bool down = true;
        for (int tick = 0; tick < 240; ++tick) {
            if (tick % 5 == 0) {
                down = !down;
                recognizer.rawEdge(down);
            }
            recognizer.observe(movement, .01, true, false);
        }
        expect(recognizer.result().state == State::Tracking, "stable-pre");
        double const hold0 = recognizer.result().holdSeconds;
        double const release0 = recognizer.result().releaseSeconds;
        // Three early hold closes (~30ms instead of ~50ms) — must not rewrite lock.
        for (int mess = 0; mess < 3; ++mess) {
            for (int tick = 0; tick < 3; ++tick)
                recognizer.observe(movement, .01, true, false);
            down = !down;
            recognizer.rawEdge(down);
            for (int tick = 0; tick < 5; ++tick)
                recognizer.observe(movement, .01, true, false);
            down = !down;
            recognizer.rawEdge(down);
        }
        expect(recognizer.result().state == State::Tracking, "stable-still");
        expect(std::abs(recognizer.result().holdSeconds - hold0) < .015, "stable-hold");
        expect(std::abs(recognizer.result().releaseSeconds - release0) < .015, "stable-release");
    }

    // Angle snap retargets duty onto a discrete heading.
    {
        EbWavePatternRecognizer recognizer;
        EbWavePatternRecognizer::Settings settings;
        settings.angleSnapDegrees = 10;
        recognizer.configure(settings);
        EbFlightMeasurement::Result movement;
        movement.status = EbFlightMeasurement::Status::Valid;
        movement.headingValid = true;
        movement.travelAngle = 14.;
        movement.vx = 800.;
        movement.vy = 200.;
        bool down = true;
        for (int tick = 0; tick < 240; ++tick) {
            if (tick % 5 == 0) {
                down = !down;
                recognizer.rawEdge(down);
            }
            recognizer.observe(movement, .01, true, false);
        }
        expect(recognizer.result().state == State::Tracking, "snap-tracking");
        expect(std::abs(recognizer.result().targetTravelDegrees - 10.) < 1e-6, "snap-target-10");
    }

    expect(stateName(State::Tracking)[0] != '\0', "state-name");

    if (failures == 0) {
        report = "wavePatternRecognizer OK";
        return true;
    }
    report = "wavePatternRecognizer " + std::to_string(failures) + " fails: " + report;
    return false;
}
