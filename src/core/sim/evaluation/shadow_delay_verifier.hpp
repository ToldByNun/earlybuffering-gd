#ifndef SHADOW_DELAY_VERIFIER_HPP
#define SHADOW_DELAY_VERIFIER_HPP

#include "../planning/simulation_plan.hpp"

#include "../shadow_simulator.hpp"

class EbShadowDelayVerifier {
public:
    static EbShadowSimulator::VerifyDelayResult
    verifyDelay(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down,
                int delay, int limit, float deltaTime, float firstDt, EbPlanOptions const& options);

private:
    EbShadowDelayVerifier() = delete;
};

#endif
