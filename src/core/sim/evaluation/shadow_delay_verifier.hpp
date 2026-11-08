#ifndef SHADOW_DELAY_VERIFIER_HPP
#define SHADOW_DELAY_VERIFIER_HPP

#include "../planning/simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class ShadowDelayVerifier {
public:
    static ShadowSimulator::VerifyDelayResult
    verifyDelay(ShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                int delay, int limit, float deltaTime, float firstDt, PlanOptions const& options);

private:
    ShadowDelayVerifier() = delete;
};

#endif
