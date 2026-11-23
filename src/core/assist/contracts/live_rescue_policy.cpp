#include "live_rescue_policy.hpp"

EbLiveRescuePolicy::Observation EbLiveRescuePolicy::classify(bool died, bool unknown,
                                                             bool diedBeforeAction, int ticksRan,
                                                             int horizon, bool completedAction) {
    if (unknown || ticksRan <= 0 || horizon <= 0)
        return Observation::Incomplete;
    if (died || diedBeforeAction)
        return Observation::Dead;
    // A completed action has its own physical endpoint: a valid jump/flip,
    // an airborne phase and consecutive collision-checked contact ticks.
    // Waiting for the next required click would reject every rhythmic corridor.
    if (completedAction)
        return Observation::Safe;
    if (ticksRan < horizon)
        return Observation::Incomplete;
    return Observation::Safe;
}

int EbLiveRescuePolicy::choose(std::span<Observation const> observations) {
    if (observations.empty() || observations.front() == Observation::Incomplete)
        return -1;
    if (observations.front() == Observation::Safe)
        return 0;
    for (std::size_t delay = 1; delay < observations.size(); ++delay) {
        if (observations[delay] == Observation::Safe)
            return static_cast<int>(delay);
    }
    return -1;
}
