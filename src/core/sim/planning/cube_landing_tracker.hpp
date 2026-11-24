#ifndef CUBE_LANDING_TRACKER_HPP
#define CUBE_LANDING_TRACKER_HPP

class EbCubeLandingTracker {
public:
    static bool validAction(bool jumped, bool orbActivated, bool allowOrbLanding, bool sameGravity,
                            bool dashing, bool unknown, bool cubeMode);
    bool observe(bool validJump, bool grounded, int minimumAirTicks = 6,
                 int stableContactTicks = 4);

private:
    int m_airTicks = 0;
    int m_groundTicks = 0;
};
#endif
