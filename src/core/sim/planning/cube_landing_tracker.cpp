#include "cube_landing_tracker.hpp"

bool EbCubeLandingTracker::observe(bool validJump, bool grounded, int minimumAirTicks,
                                   int stableContactTicks) {
    if (!validJump) {
        m_airTicks = 0;
        m_groundTicks = 0;
        return false;
    }
    if (!grounded) {
        ++m_airTicks;
        m_groundTicks = 0;
        return false;
    }
    if (m_airTicks < minimumAirTicks) {
        m_airTicks = 0;
        m_groundTicks = 0;
        return false;
    }
    return ++m_groundTicks >= stableContactTicks;
}
