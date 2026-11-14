#include "ball_landing_tracker.hpp"

void EbBallLandingTracker::reset(bool initialGravity) {
    m_initialGravity = initialGravity;
    m_airborne = false;
    m_contactTicks = 0;
}

bool EbBallLandingTracker::observe(bool currentGravity, bool grounded) {
    if (currentGravity == m_initialGravity) {
        m_airborne = false;
        m_contactTicks = 0;
        return false;
    }
    if (!grounded) {
        m_airborne = true;
        m_contactTicks = 0;
        return false;
    }
    if (!m_airborne)
        return false;
    ++m_contactTicks;
    return m_contactTicks >= kStableContactTicks;
}
