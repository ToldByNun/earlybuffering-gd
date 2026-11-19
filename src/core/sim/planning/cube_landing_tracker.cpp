#include "cube_landing_tracker.hpp"

bool EbCubeLandingTracker::observe(bool validJump, bool grounded) {
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
    if (m_airTicks < 6) {
        m_airTicks = 0;
        m_groundTicks = 0;
        return false;
    }
    return ++m_groundTicks >= 4;
}
