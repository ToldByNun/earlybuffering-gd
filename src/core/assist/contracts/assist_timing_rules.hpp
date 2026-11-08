#ifndef ASSIST_TIMING_RULES_HPP
#define ASSIST_TIMING_RULES_HPP

#include <string>

class AssistTimingRules {
public:
    static int resolveEffectTick(int attemptTick, bool midStep, bool continuous);
    static bool effectTickSelfCheck(std::string& report);
    static int resolveFireTick(int effectTick, int delay, int jumpTick, bool requireOrb);
    static bool shouldFireBuffered(int attemptTick, int fireTick, int pressTick, int planDelay,
                                   int limit);
    static bool fireTickSelfCheck(std::string& report);
    static int resolveQueuedHoldTicks(bool requireHold, bool continuous, int releaseAfter,
                                      int releaseEarliest, int releaseLatest, int extendCap);
    static bool queuedHoldSelfCheck(std::string& report);
    static int acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire, int absMax,
                                  int robustK, int deadline, bool requireOrb, bool planOk);
    static bool recheckSlideSelfCheck(std::string& report);

private:
    AssistTimingRules() = delete;
};

#endif
