#ifndef SHADOW_PLAN_POLICY_HPP
#define SHADOW_PLAN_POLICY_HPP

#include "simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class EbShadowPlanPolicy {
public:
    static EbShadowSimulator::Model modelFor(PlayerObject* player, int limit, float capMul);
    static bool isHazardLike(GameObject* object);
    static EbThreatZone buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj);
    static bool spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit, float deltaTime);
    static bool spatialHazardProximity(PlayLayer* layer, PlayerObject* real, float proximityX);
    static bool shouldWaveHazardProximityPass(bool isWave, bool nearHazard, bool noOrbTarget,
                                              bool foresightOrbPad, bool pressDown);
    static bool shouldWaveFutileDelayPass(bool isWave, bool orbIntent, int bestSurviveBin,
                                          float bestEndXGain, bool foresightOrbPad, bool pressDown);
    static bool deathLooksLikeAscent(float playerY, float deathY, bool upsideDown, float marginY);
    static bool classifyIdleSafer(bool continuous, bool pressDown, bool baselineDied,
                                  int baselineDeathTick, float baselineDeathX, bool d0Died,
                                  int d0DeathTick, float d0DeathX);
    static bool classifyAscentUseful(bool continuous, bool pressDown, bool d0Viable,
                                     int d0SurviveBin, float d0EndX, bool laterViable,
                                     int laterSurviveBin, float laterEndX, bool laterAlive);
    static int pickPreferredRelease(int earliest, int latest, bool continuous);
    static int autoRobustKHints(bool waveLike, bool isWave, float playerSpeed, float dtVariance,
                                bool spamDense);
    static int autoRobustK(PlayerObject* real, float dtVariance, bool spamDense);

private:
    EbShadowPlanPolicy() = delete;
};

#endif
