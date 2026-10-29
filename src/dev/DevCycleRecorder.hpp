#ifndef DEV_CYCLE_RECORDER_HPP
#define DEV_CYCLE_RECORDER_HPP

#include "DevTraceStore.hpp"
#include "../core/AssistVerdict.hpp"
#include "../core/ShadowSimulator.hpp"

#include <cstdint>
#include <string>

// Live press-cycle writer for DevTraceStore (Commit 4: through DelayArm).
// No-ops unless Dev Record Enable + open session with an unsealed attempt.
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

    int64_t activePressId() const { return this->m_cycle.pressId; }
    int64_t activePressEventId() const { return this->m_cycle.pressEventId; }

    void noteGatePass(AssistReason reason, char const* where);
    void notePlanFinished(SimPlan const& plan, int effectTick, int limit);
    void notePassThrough(AssistReason reason, char const* where);
    void noteDelayArm(
        AssistReason reason,
        char const* where,
        int plannedDelay,
        int plannedFireTick,
        int planId,
        DevEventSource source = DevEventSource::Live
    );

    // Drop active cycle without writing (e.g. swallowed duplicate down).
    void clearActive();

    static bool selfCheck(std::string& report);

private:
    DevCycleRecorder() = default;

    bool canWrite() const;
    DevDecisionEvent* appendKind(
        DevEventKind kind,
        int64_t parentEventId,
        int64_t planId,
        AssistReason reason,
        char const* where
    );

    struct ActiveCycle {
        int64_t pressId = 0;
        int64_t pressEventId = 0;
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
    };

    ActiveCycle m_cycle{};
};

#endif // DEV_CYCLE_RECORDER_HPP
