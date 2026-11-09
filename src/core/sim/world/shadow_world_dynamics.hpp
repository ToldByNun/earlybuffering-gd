#ifndef SHADOW_WORLD_DYNAMICS_HPP
#define SHADOW_WORLD_DYNAMICS_HPP

#include "../shadow_simulator.hpp"

class EbShadowWorldDynamics {
public:
    static bool isDynamicTriggerTarget(EbShadowSimulator const& simulator, int uniqueId);
    static bool isToggleTriggerTarget(EbShadowSimulator const& simulator, int uniqueId);
    static bool liveDynamicApplied(EbShadowSimulator const& simulator, int uniqueId);
    static void noteUnknown(EbShadowSimulator& simulator, GameObject* object);
    static void applyLiveDynamicNear(EbShadowSimulator& simulator, PlayLayer* layer,
                                     PlayerObject* ghost);
    static void applyTriggerWorldAtTick(EbShadowSimulator& simulator, PlayLayer* layer, int absTick,
                                        PlayerObject* ghost);

private:
    EbShadowWorldDynamics() = delete;
};

#endif
