#ifndef IDLE_PREFIX_SHARE_HPP
#define IDLE_PREFIX_SHARE_HPP

#include <string>

class EbShadowPlanContext;

// Ascending-delay idle-prefix sharing: simulate idle ticks once, fork (player +
// world) at each press delay. Same trajectories as independent runOne(d) for
// d=1..limit; ~½–N× fewer idle physics ticks at large Max Assist.
// Delay 0 stays on the normal evalDelay path (no shared pre-collide).
class EbIdlePrefixShare {
public:
    // Fill context.cands for unfilled delays in 1..limit via shared idle spine.
    // Returns true if idle-share ran (even if it early-exited). False = caller
    // should use the legacy per-delay loop (traj engine / invalid args).
    static bool evaluateAscending(EbShadowPlanContext& context);

    // Pure scheduling invariants (no PlayLayer).
    static bool selfCheck(std::string& report);

private:
    EbIdlePrefixShare() = delete;
};

#endif
