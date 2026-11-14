#include "orb_continuation.hpp"

EbOrbContinuation::Input EbOrbContinuation::next(int tick, int ringCount, bool freshOrb) {
    if (tick < 0 || ringCount < 1)
        return Input::None;
    if (m_releaseTick < 0) {
        m_releaseTick = tick;
        return Input::Release;
    }
    if (m_pressTick < 0 && tick > m_releaseTick && freshOrb && ringCount == 1) {
        m_pressTick = tick;
        return Input::Press;
    }
    if (m_pressTick >= 0 && tick > m_pressTick && !m_releasedFollowup) {
        m_releasedFollowup = true;
        return Input::Release;
    }
    return Input::None;
}

bool EbOrbContinuation::attempted() const {
    return m_pressTick >= 0;
}

bool EbOrbContinuation::verified(int ringCount, int ticksRan) const {
    return attempted() && ringCount >= 2 && ticksRan - m_pressTick >= 24;
}

int EbOrbContinuation::pressTick() const {
    return m_pressTick;
}
