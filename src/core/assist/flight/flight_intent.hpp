#ifndef FLIGHT_INTENT_HPP
#define FLIGHT_INTENT_HPP
#include "flight_measurement.hpp"
class EbFlightIntent {
public:
    enum class State { Inactive, Acquiring, Tracking, Yielding, Reacquiring };
    enum class Reason { None, ContextChanged, SustainedInput, RhythmChanged, StaleEvidence };
    class Settings {
    public:
        double acquisitionSeconds = .3;
        double residualSeconds = .15;
        double durationTolerance = .5;
        double minimumDeviationSeconds = .025;
        double maximumTargetAge = 3.;
        bool manualTarget = false;
        double targetDegrees = 0.;

    private:
    };
    class Result {
    public:
        State state = State::Inactive;
        Reason reason = Reason::None;
        double targetDegrees = 0.;
        double confidence = 0.;
        double holdSeconds = 0.;
        double releaseSeconds = 0.;
        double rawEvidenceAge = 0.;
        double cleanSeconds = 0.;
        bool targetValid = false;
        bool yieldRequested = false;

    private:
    };
    void configure(Settings const& settings);
    void rawEdge(bool down);
    void observe(EbFlightMeasurement::Result const& measurement, double dt, bool supported,
                 bool assisted);
    void reset();
    Result const& result() const;
    static char const* stateName(State state);

private:
    void yield(Reason reason);
    void clearAcquisition();
    Settings m_settings;
    Result m_result;
    double m_time = 0.;
    double m_edgeTime = 0.;
    double m_lastEvidence = 0.;
    double m_targetTime = 0.;
    double m_yieldTime = 0.;
    double m_hold = 0.;
    double m_release = 0.;
    double m_cleanAge = 0.;
    double m_windowSeconds = 0.;
    double m_sumX = 0.;
    double m_sumY = 0.;
    int m_holdCount = 0;
    int m_releaseCount = 0;
    int m_deviations = 0;
    bool m_hasEdge = false;
    bool m_down = false;
};
#endif
