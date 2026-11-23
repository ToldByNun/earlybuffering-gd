#ifndef LIVE_RESCUE_POLICY_HPP
#define LIVE_RESCUE_POLICY_HPP

#include <span>

class EbLiveRescuePolicy {
public:
    enum class Observation { Incomplete, Dead, Safe };
    static Observation classify(bool died, bool unknown, bool diedBeforeAction, int ticksRan,
                                int horizon, bool completedAction = false);
    // Zero preserves a safe original input; -1 means no proved replacement.
    static int choose(std::span<Observation const> observations);

private:
};

#endif
