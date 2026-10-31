#ifndef DEV_CYCLE_RECORDER_HPP
#define DEV_CYCLE_RECORDER_HPP

#include "DevClockContract.hpp"
#include "DevLiveWitness.hpp"
#include "DevTraceStore.hpp"
#include "../core/AssistVerdict.hpp"
#include "../core/ShadowSimulator.hpp"

class PlayLayer;
class PlayerObject;

#include <cstdint>
#include <string>
#include <unordered_map>

// Live press-cycle writer for DevTraceStore.
// Commit 4: through DelayArm. Commit 5: recheck / HUS / fire / outcome + attempt seal.
class DevCycleRecorder {
public:
    static DevCycleRecorder& get();

    bool isRecording() const;

    // Starts a new pressObserved (parent=0). Returns pressId, or 0 if not recording.
    int64_t beginPress(
        int playerIndex,
        bool down,
        int effectTick,
        int pressTick,
        DevEventSource source = DevEventSource::Live
    );

    int64_t activePressId() const { return this->m_activePressId; }

    void noteGatePass(AssistReason reason, char const* where);
    void notePlanFinished(
        SimPlan const& plan,
        int effectTick,
        int limit,
        DevClockContract::Stamp const& clock,
        PlayLayer* layer,
        PlayerObject* player
    );
    // After learning/verify adjusts the armed delay relative to plan.delay.
    void noteLearningOnPlan(
        int plannedDelay,
        int finalDelay,
        bool floored,
        bool verifyRefused
    );
    void notePassThrough(AssistReason reason, char const* where);

    // Optional arm diagnostics (verify / budget / hold-extend).
    // Explicit ctor — nested default member + `= {}` breaks clang (Android NDK).
    struct DelayArmMeta {
        bool armVerifyOk;
        bool budgetHitAtArm;
        int holdExtendMax;

        DelayArmMeta()
            : armVerifyOk(true)
            , budgetHitAtArm(false)
            , holdExtendMax(-1) {}
    };

    // Returns pressId of the armed cycle (0 if not recording).
    int64_t noteDelayArm(
        AssistReason reason,
        char const* where,
        int plannedDelay,
        int plannedFireTick,
        int planId,
        DevEventSource source = DevEventSource::Live,
        DelayArmMeta meta = DelayArmMeta()
    );

    void noteRecheck(
        int64_t pressId,
        int oldFireTick,
        int newFireTick,
        char const* where
    );
    void noteHus(
        int64_t pressId,
        AssistReason reason,
        char const* where,
        int fireTick
    );
    void noteFire(int64_t pressId, int actualFireTick);
    void noteHoldExtend(int64_t pressId, int holdExtendMax);
    void noteCheckpoint(int attemptTick);

    // Append outcome for every open cycle in the current attempt, then seal+flush.
    // countTowardTarget follows DevConfig::countEmptyAttempts / has events.
    // attemptTick used for deathAfterFire / ticksToDeath when lived=false.
    bool sealAttemptWithOutcomes(
        std::string endReason, bool lived, int attemptTick = -1
    );
    // New attempt under an open session (no-op if none / already open).
    bool beginNewAttempt(int startTick);
    // Death→reset path: seal open attempt if any, then begin the next.
    void onAttemptReset(int startTick);
    // Quit / abort: seal as quit, flush, leave session open or closed.
    bool flushQuit();

    void clearActive();

    static bool selfCheck(std::string& report);

private:
    DevCycleRecorder() = default;

    struct TrackedCycle {
        int64_t pressId = 0;
        int64_t lastEventId = 0;
        int64_t planId = 0;
        int playerIndex = 0;
        bool down = true;
        DevEventSource source = DevEventSource::Live;
        int effectTick = 0;
        int pressTick = 0;
        bool hasPlan = false;
        bool hasGate = false;
        bool hasArm = false;
        bool hasPassThrough = false;
        bool hasFire = false;
        bool hasOutcome = false;
        bool armVerifyOk = true;
        bool budgetHitAtArm = false;
        int holdExtendMax = -1;
        int actualFireTick = -1;
        int recheckDelta = 0;
    };

    bool canWriteActive() const;
    TrackedCycle* tracked(int64_t pressId);
    TrackedCycle* ensureActiveTracked();
    DevDecisionEvent* appendTo(
        TrackedCycle& cycle,
        DevEventKind kind,
        int64_t parentEventId,
        int64_t planId,
        AssistReason reason,
        char const* where
    );
    void writeOutcomesForOpenCycles(
        std::string const& endReason, bool lived, int attemptTick
    );
    bool shouldCountAttempt() const;

    int64_t m_activePressId = 0;
    std::unordered_map<int64_t, TrackedCycle> m_cycles;
};

#endif // DEV_CYCLE_RECORDER_HPP
