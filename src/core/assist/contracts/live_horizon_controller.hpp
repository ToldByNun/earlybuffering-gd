#ifndef LIVE_HORIZON_CONTROLLER_HPP
#define LIVE_HORIZON_CONTROLLER_HPP

class EbLiveHorizonController {
public:
    float scale(int mode, bool player1, bool down) const;
    void observe(int mode, bool player1, bool down, bool budgetHit, int spentUs, int budgetUs);
    void reset();
    static int scaledCap(int nominalCap, float scale);

private:
    float m_scales[8][2][2]{};
};
#endif
