#ifndef SHIP_LINE_POLICY_HPP
#define SHIP_LINE_POLICY_HPP
class EbShipLinePolicy {
public:
    class Target {
    public:
        bool enabled = false;
        bool reversed = false;
        bool sideways = false;
        double x = 0.;
        double y = 0.;
        double degrees = 0.;
        double bodySize = 30.;

    private:
    };
    class Metrics {
    public:
        double errorSum = 0.;
        double maxError = 0.;
        double angleSum = 0.;
        double lastHeadingError = 0.;
        int samples = 0;
        bool invalid = false;

    private:
    };
    static void observe(Target const& target, Metrics& metrics, double x, double y, double dx,
                        double dy);
    static double cost(Metrics const& metrics, int delay, int limit);
    static bool improves(Metrics const& baseline, Metrics const& candidate, int delay, int limit);

private:
};
#endif
