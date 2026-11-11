#ifndef ADAPTIVE_CANDIDATE_SLOT_HPP
#define ADAPTIVE_CANDIDATE_SLOT_HPP

#include "../../../shadow_simulator.hpp"
#include <string>

// Per-delay Adaptive search state. Checkpoint = rotating snapshot ring slot
// holding the ghost pose after ticksSimulated physics ticks.
//
// Resume contract (AP2):
//   extendQuantum may continue from checkpoint only while checkpointValid.
//   resetCaptureForNextRun / switching candidates must call invalidateCheckpoint()
//   — player pose alone is not enough after world orb/TW flags are restored.
// Metrics (survive ticks, endX, …) survive invalidation for ranking diagnostics.
// Screen→Verify: after screen reset the world is cold, so Phase C's first grant
// for a delay must rebuild past ticksSimulated then extend (not re-run a short
// quantum from 0 and pretend the screen depth still holds).
class EbAdaptiveCandidateSlot {
public:
    int delay = 0;
    int snapshotSlot = -1;
    int ticksSimulated = 0;
    int simAbsTickAtCheckpoint = -1;
    int releaseAfter = -1;
    bool acted = false;
    bool tapReleased = false;
    bool holdingJump = false;
    bool checkpointValid = false;
    bool screened = false;
    bool terminalDied = false;
    bool terminalUnknown = false;
    bool terminalAliveCap = false;
    int verifyGrants = 0; // Phase C quanta granted (scheduler fairness)
    // Autonomous survival (no hypothetical follow-ups). Recoverability filled in AP8.
    int autonomousSurviveTicks = 0;
    bool autonomousAlive = false;
    bool recoverability = false;
    float endX = 0.f;
    float deathX = 0.f;
    int deathTick = -1;
    bool activatedRing = false;
    bool zoneCleared = false;
    EbShadowSimulator::RunResult lastRaw{};

    void reset();
    void bindDelay(int delayTicks);
    bool hasCheckpoint() const;
    void invalidateCheckpoint();

    // Apply a finished quantum onto this slot (metrics always; checkpoint only if keepCheckpoint).
    void applyQuantumResult(EbShadowSimulator::RunResult const& result, int snapshotSlotIndex,
                            int ticksNow, int simAbsTick, bool actedNow, bool tapReleasedNow,
                            bool holdingJumpNow, bool keepCheckpoint);

    // Pure merge invariant for PlanGold: two contiguous segments equal one longer run.
    // extension.ticksRan is the count of NEW ticks in the second segment (not absolute).
    static EbShadowSimulator::RunResult mergeExtendResults(EbShadowSimulator::RunResult const& base,
                                                           EbShadowSimulator::RunResult const& extension);

    static bool selfCheck(std::string& report);

private:
};

#endif
