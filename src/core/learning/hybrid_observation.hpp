#ifndef HYBRID_OBSERVATION_HPP
#define HYBRID_OBSERVATION_HPP
#include <cstdint>

class EbHybridObservation {
public:
    bool begin(int actionDelay, int id, int baselineDeath, bool orb, bool checkLanding);
    void fire(int id, int actualDelay, float phase);
    void physics(bool grounded, bool ringTouched, int ringCount);
    bool finished(int now) const;
    int rewardAt(int now, bool died) const;
    uint64_t key = 0;
    uint64_t coarseKey = 0;
    int tick = 0;
    int delay = -1;
    int planId = -1;
    int horizon = 0;
    int hint = -1;
    int phaseBin = -1, sequence = -1;
    int baselineDeathTick = -1;
    bool predictedDeath = false;
    bool delivered = false;
    bool needRing = false;
    bool touched = false;
    int touchedCount = 0;
    int mode = -1;
    bool ballLanding = false;
    bool leftGround = false;
    bool landed = false;
    bool activation = false;

private:
};
#endif
