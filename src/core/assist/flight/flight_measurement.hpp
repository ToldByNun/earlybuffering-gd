#ifndef FLIGHT_MEASUREMENT_HPP
#define FLIGHT_MEASUREMENT_HPP

class EbFlightMeasurement {
public:
    enum class Status { Acquiring, Valid, Stationary, Discontinuity, Invalid };
    class Sample {
    public:
        double x = 0.;
        double y = 0.;
        double dt = 0.;
        double speed = 1.;
        double size = 1.;
        double timeWarp = 1.;
        double gravity = 1.;
        int mode = 0;
        bool inverted = false;
        bool sideways = false;
        bool reversed = false;
        bool valid = true;

    private:
    };
    class Result {
    public:
        Status status = Status::Acquiring;
        double vx = 0.;
        double vy = 0.;
        double worldAngle = 0.;
        double travelAngle = 0.;
        double smoothedAngle = 0.;
        double velocityVariance = 0.;
        double observedSeconds = 0.;
        bool headingValid = false;

    private:
    };
    void observe(Sample const& sample);
    void reset();
    Result const& result() const;
    static double angleDifference(double angle, double reference);

private:
    static bool compatible(Sample const& left, Sample const& right);
    Sample m_previous;
    Result m_result;
    bool m_hasPrevious = false;
    double m_smoothX = 0.;
    double m_smoothY = 0.;
};
#endif
