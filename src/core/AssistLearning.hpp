#ifndef EARLYBUFFER_CORE_ASSISTLEARNING_HPP
#define EARLYBUFFER_CORE_ASSISTLEARNING_HPP
#include "../config/StageConfig.hpp"
class AssistLearning {
public:
    static void decaySpot(GameModeKind kind, float x);
    static bool shouldExploreDespiteLowTrust(float trust);
    static bool allowTrustUpFromAssistInject();
};
#endif