#ifndef CUBE_LANDING_TRACKER_HPP
#define CUBE_LANDING_TRACKER_HPP

class EbCubeLandingTracker {
public:
    bool observe(bool validJump, bool grounded);

private:
    int m_airTicks = 0;
    int m_groundTicks = 0;
};
#endif
