#ifndef SIMULATION_CANDIDATE_HPP
#define SIMULATION_CANDIDATE_HPP

#include "../shadow_run_result.hpp"

class EbSimulationCandidate {
public:
    int delay = 0;
    bool viable = false;
    bool diedBeforeAction = false;
    bool dualFailed = false;
    bool clearsThreat = false;
    bool unknownTrunc = false;
    bool toggleUnknown = false;
    bool activatedRing = false;
    bool orbUseful = false;
    bool requireHold = false;
    bool robustKFailed = false;
    bool verifyFailed = false;
    int releaseAfter = -1; // ticks after press; -1 = auto tap / unknown
    int surviveBin = 0;
    int surviveTicks = 0;
    float endX = 0.f;
    float deathX = 0.f;
    int jumpTick = 0;
    EbShadowRunResult raw{};

private:
};

#endif
