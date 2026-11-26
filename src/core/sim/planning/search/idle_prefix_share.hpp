#ifndef IDLE_PREFIX_SHARE_HPP
#define IDLE_PREFIX_SHARE_HPP

#include <memory>
#include <string>

class EbShadowPlanContext;

// Ascending-delay idle-prefix sharing: simulate idle ticks once, fork (player +
// world) at each press delay. Same trajectories as independent runOne(d) for
// d=1..limit; ~½–N× fewer idle physics ticks at large Max Assist.
// Delay 0 stays on the normal evalDelay path (no shared pre-collide).
class EbIdlePrefixShare {
public:
    class Cursor {
    public:
        explicit Cursor(EbShadowPlanContext& context);
        ~Cursor();
        Cursor(Cursor const&) = delete;
        Cursor& operator=(Cursor const&) = delete;
        bool probe(int delay);
        void invalidate();

    private:
        friend class EbIdlePrefixShare;
        class State;
        EbShadowPlanContext& m_context;
        std::unique_ptr<State> m_state;
        int m_idleTicks = 0;
        bool m_seeded = false;
        bool m_supported = true;
    };

    // Fill context.cands for unfilled delays in 1..limit via shared idle spine.
    // Returns true if idle-share ran (even if it early-exited). False = caller
    // should use the legacy per-delay loop (traj engine / invalid args).
    static bool evaluateAscending(EbShadowPlanContext& context);

    // Pure scheduling invariants (no PlayLayer).
    static bool selfCheck(std::string& report);

private:
    static bool probeRescue(Cursor& cursor, int delay);
    static void resetRescue(Cursor& cursor);
    EbIdlePrefixShare() = delete;
};

#endif
