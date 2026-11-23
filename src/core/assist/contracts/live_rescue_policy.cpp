#include "live_rescue_policy.hpp"

EbLiveRescuePolicy::Observation EbLiveRescuePolicy::classify(bool died, bool unknown,
                                                             bool diedBeforeAction, int ticksRan,
                                                             int horizon, bool completedBallFlip) {
    if (unknown || ticksRan <= 0 || horizon <= 0)
        return Observation::Incomplete;
    if (died || diedBeforeAction)
        return Observation::Dead;
    // A completed Ball action has its own physical endpoint: opposite gravity,
    // an airborne phase and eight consecutive collision-checked contact ticks.
    // Waiting for the next required click would reject every rhythmic corridor.
    if (completedBallFlip)
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
