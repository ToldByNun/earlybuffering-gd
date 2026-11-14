#ifndef SHADOW_RUN_RESULT_HPP
#define SHADOW_RUN_RESULT_HPP

#include <Geode/Geode.hpp>

// Standalone so planning headers can embed a run result without pulling
// shadow_simulator.hpp (which includes simulation_plan → candidate → cycle).
class EbShadowRunResult {
public:
    bool landedAfterAction = false;
    bool died = false;
    bool diedBeforeAction = false;
    bool jumped = false;
    bool unknown = false;
    bool activatedRing = false;
    bool unknownTrunc = false;
    bool toggleUnknown = false;
    bool zoneCleared = false;
    bool wasDashing = false;
    int ringCount = 0;
    int orbContinuationTick = -1;
    int jumpTick = -1;
    int deathTick = -1;
    int unknownTick = -1;
    int ticksRan = 0;
    int ticksBeforeNeutral = 0;
    float endX = 0.f;
    float deathX = 0.f;
    GameObject* deathObject = nullptr;

private:
};

#endif
