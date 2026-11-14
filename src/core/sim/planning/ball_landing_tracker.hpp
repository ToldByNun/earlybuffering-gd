#ifndef BALL_LANDING_TRACKER_HPP
#define BALL_LANDING_TRACKER_HPP

class EbBallLandingTracker {
public:
    static constexpr int kStableContactTicks = 8;
    void reset(bool initialGravity);
    bool observe(bool currentGravity, bool grounded);

private:
    bool m_initialGravity = false;
    bool m_airborne = false;
    int m_contactTicks = 0;
};

#endif
