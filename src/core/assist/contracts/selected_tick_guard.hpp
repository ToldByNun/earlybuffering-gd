#ifndef SELECTED_TICK_GUARD_HPP
#define SELECTED_TICK_GUARD_HPP

#include <span>

class EbSelectedTickGuard {
public:
    enum class ProbeState { Verified, Rejected, Incomplete };
    static ProbeState classifyProbe(bool died, bool unknown, bool landed, int ticksRan,
                                    int observationFloor, bool clearsThreat);
    static bool hasMargin(int delay, std::span<int const> verified);
    static int choose(int selected, int limit, int radius, std::span<int const> verified);
    static int keepVerifiedLearningDelay(int proposed, int planned, std::span<int const> verified);

private:
};

#endif
