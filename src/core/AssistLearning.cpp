#include "AssistLearning.hpp"
#include "LevelProfile.hpp"
#include <cstdlib>

void AssistLearning::decaySpot(GameModeKind kind, float x) {
    if (!StageConfig::autoTune()) return;
    auto& s = LevelProfile::get().spot(kind, x);
    // Pull trust gently toward 0.5 so low-trust spots can recover via explore.
    s.trust = s.trust * 0.98f + 0.5f * 0.02f;
}

bool AssistLearning::shouldExploreDespiteLowTrust(float trust) {
    if (trust >= 0.35f) return false;
    // ~10% explore
    return (std::rand() % 10) == 0;
}

bool AssistLearning::allowTrustUpFromAssistInject() {
    // Never reinforce from own inject "saves".
    return false;
}