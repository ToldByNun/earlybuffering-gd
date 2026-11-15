#include "hybrid_observation.hpp"
#include <algorithm>

bool EbHybridObservation::begin(int actionDelay, int id, int baselineDeath, bool orb,
                                bool checkLanding) {
    if (actionDelay < 0 || actionDelay > 88 || baselineDeath >= 96)
        return false;
    delay = actionDelay;
    planId = id;
    delivered = delay == 0;
    baselineDeathTick = baselineDeath;
    predictedDeath = baselineDeath >= 0;
    horizon = std::max(delay + 24, baselineDeath + 1);
    needRing = delay > 0 && orb;
    ballLanding = delay > 0 && checkLanding;
    return true;
}

void EbHybridObservation::fire(int id, int actualDelay, float phase) {
    // Actual phase reports where dispatch happened inside the real update.
    // Integer plans can fire there too. Match the executed plan and delay.
    if (delay > 0 && planId == id && delay == actualDelay && phase >= 0.f && phase < 1.f) {
        delivered = true;
        phaseBin = std::clamp(static_cast<int>(phase * 8), 0, 7);
    }
}

void EbHybridObservation::physics(bool grounded, bool ringTouched, int ringCount) {
    if (delivered) {
        if (!grounded)
            leftGround = true;
        if (grounded && leftGround)
            landed = true;
        if ((ringTouched && !touched) || ringCount > touchedCount)
            activation = true;
    }
    touched = ringTouched;
    touchedCount = ringCount;
}

bool EbHybridObservation::finished(int now) const {
    return now > tick + horizon;
}

int EbHybridObservation::rewardAt(int now, bool died) const {
    if (!delivered || delay < 0 || now < tick + delay)
        return 0;
    if (died)
        return finished(now) ? 0 : -4;
    if (!finished(now) || (needRing && !activation))
        return 0;
    return delay > 0 && (predictedDeath || (activation && needRing) || (landed && ballLanding)) ? 3
                                                                                                : 1;
}
