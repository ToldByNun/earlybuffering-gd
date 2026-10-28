#ifndef EARLYBUFFER_CORE_ASSISTLEARNING_HPP
#define EARLYBUFFER_CORE_ASSISTLEARNING_HPP

#include "../config/StageConfig.hpp"
#include <string>

class AssistLearning {
public:
    static void decaySpot(GameModeKind kind, float x);
    static bool shouldExploreDespiteLowTrust(float trust);
    // Extra explore while earliness histogram is still thin (raw roll).
    static bool shouldExploreHistogram(int histN);

    // Gates random ±1 hist explore. False for Wave/Ship/Swing, spam Pass,
    // bake-prefer band, or thin hist after assist-related deaths.
    static bool histogramExploreEligible(
        GameModeKind kind,
        int histN,
        bool spamWouldPass,
        bool bakePreferActive,
        int diedAfterAssist
    );
    static bool allowHistogramExplore(
        GameModeKind kind,
        int histN,
        bool spamWouldPass,
        bool bakePreferActive,
        int diedAfterAssist
    );

    static bool allowTrustUpFromAssistInject();
    static bool exploreGateSelfCheck(std::string& report);
};

#endif // EARLYBUFFER_CORE_ASSISTLEARNING_HPP
