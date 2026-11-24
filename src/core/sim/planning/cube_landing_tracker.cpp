#include "cube_landing_tracker.hpp"

bool EbCubeLandingTracker::validAction(bool jumped, bool orbActivated, bool allowOrbLanding,
                                       bool sameGravity, bool dashing, bool unknown,
                                       bool cubeMode) {
    if (unknown || dashing || !cubeMode)
        return false;
    if (orbActivated)
        return allowOrbLanding;
    return jumped && sameGravity;
}

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
