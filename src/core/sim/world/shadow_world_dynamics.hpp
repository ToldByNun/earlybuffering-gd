#ifndef SHADOW_WORLD_DYNAMICS_HPP
#define SHADOW_WORLD_DYNAMICS_HPP

#include "../shadow_simulator.hpp"

class ShadowWorldDynamics {
public:
    static bool isDynamicTriggerTarget(ShadowSimulator const& simulator, int uniqueId);
    static bool isToggleTriggerTarget(ShadowSimulator const& simulator, int uniqueId);
    static bool liveDynamicApplied(ShadowSimulator const& simulator, int uniqueId);
    static void noteUnknown(ShadowSimulator& simulator, GameObject* object);
    static void applyLiveDynamicNear(ShadowSimulator& simulator, PlayLayer* layer,
                                     PlayerObject* ghost);
    static void applyTriggerWorldAtTick(ShadowSimulator& simulator, PlayLayer* layer, int absTick,
                                        PlayerObject* ghost);

private:
    ShadowWorldDynamics() = delete;
};

#endif
