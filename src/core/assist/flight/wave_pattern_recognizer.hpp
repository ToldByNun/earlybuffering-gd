#ifndef WAVE_PATTERN_RECOGNIZER_HPP
#define WAVE_PATTERN_RECOGNIZER_HPP

#include "flight_measurement.hpp"
#include <string>

// AP07/AP08: recognize Wave hold/release spam and keep a locked motif/heading stable.
class EbWavePatternRecognizer {
public:
    enum class State { Inactive, Acquiring, Tracking, Yielding, Reacquiring };
    enum class Reason {
        None,
        ContextChanged,
        SustainedInput,
        RhythmChanged,
        MotifChanged,
        StaleEvidence
    };

    class Settings {
    public:
        double durationTolerance = .35;
        double minimumDeviationSeconds = .012;
        double maximumSegmentSeconds = 1.25;
        double maximumTargetAge = 6.;
        double residualSeconds = .08;
        int lockCycles = 3;
        int motifMaxPairs = 2;
        // 0 = continuous travel heading. Else lock onto this degree grid.
        int angleSnapDegrees = 0;

    private:
    };

    class Result {
    public:
        State state = State::Inactive;
        Reason reason = Reason::None;
        double confidence = 0.;
        double holdSeconds = 0.;
        double releaseSeconds = 0.;
        double periodSeconds = 0.;
        double dutyCycle = 0.;
        double phaseSeconds = 0.;
        double jitterSeconds = 0.;
        double averageTravelDegrees = 0.; // recent measured average (diagnostic)
        double targetTravelDegrees = 0.;  // locked assist heading (snapped when configured)
        int motifPairs = 1;
        int matchedCycles = 0;
        double rawEvidenceAge = 0.;
        double lastCompletedSeconds = 0.;
        bool lastCompletedHold = false;
        bool hasLastCompleted = false;
        bool holding = false;
        bool targetValid = false;
        bool yieldRequested = false;

    private:
    };

    void configure(Settings const& settings);
    // Hot-path: update snap without wiping a live lock.
    void setAngleSnapDegrees(int stepDegrees);
    void rawEdge(bool down);
    void observe(EbFlightMeasurement::Result const& measurement, double dt, bool supported,
                 bool assisted);
    void abandon(Reason reason);
    void reset();
    Result const& result() const;
    static char const* stateName(State state);
    static bool selfCheck(std::string& report);

private:
    class Segment {
    public:
        bool hold = false;
        double seconds = 0.;
        bool assisted = false;

    private:
    };

    void yield(Reason reason);
    void pushSegment(bool hold, double seconds, bool assisted);
    void scoreMotifs();
    void refineLockedMotif();
    void lockTravelTarget(double measuredDegrees);
    void updateTravel(EbFlightMeasurement::Result const& measurement, double dt, bool assisted);
    void publishDurations(double holdSeconds, double releaseSeconds);
    Settings m_settings;
    Result m_result;
    static constexpr int kSegmentCapacity = 16;
    Segment m_segments[kSegmentCapacity]{};
    int m_segmentCount = 0;
    int m_segmentWrite = 0;
    double m_time = 0.;
    double m_edgeTime = 0.;
    double m_lastEvidence = 0.;
    double m_targetTime = 0.;
    double m_yieldTime = 0.;
    double m_cleanAge = 0.;
    double m_sumX = 0.;
    double m_sumY = 0.;
    double m_travelSeconds = 0.;
    double m_probeSumX = 0.;
    double m_probeSumY = 0.;
    double m_probeSeconds = 0.;
    double m_headingShiftSeconds = 0.;
    // Motif templates: up to 2 hold/release pairs → 4 segment means.
    double m_template[4]{};
    int m_templateLen = 0;
    int m_matchedCycles = 0;
    int m_deviations = 0;
    bool m_hasEdge = false;
    bool m_down = false;
    bool m_edgeAssisted = false;
    bool m_travelLocked = false;
};

#endif
