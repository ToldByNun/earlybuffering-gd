#ifndef EARLYBUFFER_CORE_ASSISTLEARNING_HPP
#define EARLYBUFFER_CORE_ASSISTLEARNING_HPP
#include "../config/StageConfig.hpp"
class AssistLearning {
public:
    static void decaySpot(GameModeKind kind, float x);
    static bool shouldExploreDespiteLowTrust(float trust);
    // Extra explore while earliness histogram is still thin.
    static bool shouldExploreHistogram(int histN);
    static bool allowTrustUpFromAssistInject();
};
#endif