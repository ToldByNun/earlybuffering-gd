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
    static bool selfCheck(std::string& report);

private:
};

#endif
