#ifndef ORB_CONTINUATION_HPP
#define ORB_CONTINUATION_HPP

class EbOrbContinuation {
public:
    enum class Input { None, Release, Press };
    Input next(int tick, int ringCount, bool freshOrb);
    bool attempted() const;
    bool verified(int ringCount, int ticksRan) const;
    int pressTick() const;

private:
    int m_releaseTick = -1;
    int m_pressTick = -1;
    bool m_releasedFollowup = false;
};

#endif
