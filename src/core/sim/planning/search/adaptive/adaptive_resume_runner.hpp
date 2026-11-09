#ifndef ADAPTIVE_RESUME_RUNNER_HPP
#define ADAPTIVE_RESUME_RUNNER_HPP

#include "adaptive_candidate_slot.hpp"
#include "../../../shadow_simulator.hpp"
#include <string>

// Truncated / resumable candidate sim for Adaptive Phase B/C.
// Does not call resetCaptureForNextRun — caller owns world restore when switching slots.
class EbAdaptiveResumeRunner {
public:
    // Fresh quantum from `real`, or extend from slot checkpoint when hasCheckpoint().
    // quantumTicks = additional physics ticks to simulate (hard-capped by model.cap / budget).
    // On non-terminal success, captures m_shadow into the rotating snapshot ring and
    // binds it on `slot` so a later call can continue without replaying 0..T.
    static EbShadowSimulator::RunResult runQuantum(
        EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down, int limit,
        float deltaTime, float firstDt, EbShadowSimulator::Model const& model, int releaseAfter,
        EbThreatZone const* zone, int* tickBudgetLeft, EbAdaptiveCandidateSlot& slot,
        int quantumTicks);

    static bool selfCheck(std::string& report);

private:
    EbAdaptiveResumeRunner() = delete;
};

#endif
