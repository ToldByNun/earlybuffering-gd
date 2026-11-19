#ifndef WAVE_PATTERN_POLICY_HPP
#define WAVE_PATTERN_POLICY_HPP

#include <string>

// AP08: bounded Wave hold/release timing corrections from a locked (or manual) motif.
class EbWavePatternPolicy {
public:
    class Target {
    public:
        bool enabled = false;
        bool closingHold = false; // completed segment was a hold (edge is release)
        double phaseErrorSeconds = 0.; // completed - expected; early < 0, late > 0
        double holdSeconds = 0.;
        double releaseSeconds = 0.;
        // AP09: locked motif informs heading preference / candidate order.
        double travelDegrees = 0.;
        double confidence = 0.;
        double gain = .85;
        double minimumEarlySeconds = .008;
        double maximumDelaySeconds = .085;
        double maximumLatePassSeconds = .04;

    private:
    };

    // Early edges → positive delay ticks. Late / on-time → 0 (never invent past).
    static int suggestedDelayTicks(Target const& target, float deltaTime, int limit);
    static double expectedSeconds(Target const& target);
    static double phaseError(double completedSeconds, double expectedSeconds);
    // 0 = continuous. Else round travel heading onto a discrete degree grid.
    static double snapDegrees(double degrees, int stepDegrees);
    // Prefer staying on the locked step until measured clearly belongs to a neighbor.
    static double snapDegreesSticky(double degrees, int stepDegrees, double lockedDegrees);
    // Equal-slope Wave model: θ ≈ atan((Th-Tr)/(Th+Tr)) ↔ duty = (1+tanθ)/2.
    static double dutyFromTravelDegrees(double degrees);
    static double travelDegreesFromDuty(double duty);
    static void holdReleaseForTravel(double travelDegrees, double periodSeconds, double& holdSeconds,
                                     double& releaseSeconds);
    static bool selfCheck(std::string& report);

private:
};

#endif
