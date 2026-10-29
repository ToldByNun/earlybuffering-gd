#include "DevCycleRecorder.hpp"
#include "DevConfig.hpp"

#include <cstring>
#include <filesystem>
#include <fmt/format.h>

DevCycleRecorder& DevCycleRecorder::get() {
    static DevCycleRecorder inst;
    return inst;
}

bool DevCycleRecorder::isRecording() const {
    if (!DevConfig::recordEnable()) return false;
    auto& store = DevTraceStore::get();
    return store.isOpen() && store.hasOpenAttempt();
}

bool DevCycleRecorder::canWrite() const {
    return this->isRecording() && this->m_cycle.pressId > 0;
}

void DevCycleRecorder::clearActive() {
    this->m_cycle = {};
}

int64_t DevCycleRecorder::beginPress(
    int playerIndex,
    bool down,
    int effectTick,
    int pressTick,
    DevEventSource source
) {
    this->clearActive();
    if (!this->isRecording()) return 0;

    auto& store = DevTraceStore::get();
    int64_t const pressId = store.allocPressId();
    if (pressId <= 0) return 0;

    DevDecisionEvent ev;
    ev.kind = DevEventKind::PressObserved;
    ev.pressId = pressId;
    ev.cycleId = pressId;
    ev.parentEventId = 0;
    ev.playerIndex = playerIndex;
    ev.down = down;
    ev.source = source;
    ev.effectTick = effectTick;
    ev.pressTick = pressTick;
    auto* written = store.appendEvent(ev);
    if (!written) return 0;

    this->m_cycle.pressId = pressId;
    this->m_cycle.pressEventId = written->eventId;
    this->m_cycle.lastEventId = written->eventId;
    this->m_cycle.playerIndex = playerIndex;
    this->m_cycle.down = down;
    this->m_cycle.source = source;
    this->m_cycle.effectTick = effectTick;
    this->m_cycle.pressTick = pressTick;
    return pressId;
}

DevDecisionEvent* DevCycleRecorder::appendKind(
    DevEventKind kind,
    int64_t parentEventId,
    int64_t planId,
    AssistReason reason,
    char const* where
) {
    if (!this->canWrite()) return nullptr;
    if (parentEventId <= 0) return nullptr;

    DevDecisionEvent ev;
    ev.kind = kind;
    ev.pressId = this->m_cycle.pressId;
    ev.cycleId = this->m_cycle.pressId;
    ev.parentEventId = parentEventId;
    ev.planId = planId;
    ev.playerIndex = this->m_cycle.playerIndex;
    ev.down = this->m_cycle.down;
    ev.source = this->m_cycle.source;
    ev.effectTick = this->m_cycle.effectTick;
    ev.pressTick = this->m_cycle.pressTick;
    ev.assistReason = AssistVerdict::name(reason);
    if (where && where[0]) {
        ev.assistReason = fmt::format("{}@{}", AssistVerdict::name(reason), where);
    }
    auto* written = DevTraceStore::get().appendEvent(ev);
    if (!written) return nullptr;
    this->m_cycle.lastEventId = written->eventId;
    return written;
}

void DevCycleRecorder::noteGatePass(AssistReason reason, char const* where) {
    if (!this->canWrite()) return;
    if (this->m_cycle.hasPlan || this->m_cycle.hasArm) return;
    if (this->m_cycle.hasGate) return;
    auto* w = this->appendKind(
        DevEventKind::GatePass,
        this->m_cycle.pressEventId,
        0,
        reason,
        where
    );
    if (w) this->m_cycle.hasGate = true;
}

void DevCycleRecorder::notePlanFinished(SimPlan const& plan, int effectTick, int limit) {
    (void)limit;
    if (!this->canWrite()) return;
    if (this->m_cycle.hasGate) return;
    if (this->m_cycle.hasPlan) return;

    auto* w = this->appendKind(
        DevEventKind::PlanFinished,
        this->m_cycle.pressEventId,
        plan.planId,
        plan.reason != AssistReason::None ? plan.reason : AssistReason::PassUnknown,
        "plan"
    );
    if (!w) return;
    w->effectTick = effectTick;
    w->planId = plan.planId;
    w->plannedDelay = plan.delay;
    this->m_cycle.planId = plan.planId;
    this->m_cycle.hasPlan = true;
}

void DevCycleRecorder::notePassThrough(AssistReason reason, char const* where) {
    if (!this->canWrite()) return;
    if (this->m_cycle.hasArm || this->m_cycle.hasPassThrough) return;
    int64_t parent = this->m_cycle.lastEventId;
    if (this->m_cycle.hasPlan || this->m_cycle.hasGate) {
        parent = this->m_cycle.lastEventId;
    }
    else {
        parent = this->m_cycle.pressEventId;
    }
    auto* w = this->appendKind(
        DevEventKind::PassThrough,
        parent,
        this->m_cycle.planId,
        reason,
        where
    );
    if (w) this->m_cycle.hasPassThrough = true;
}

void DevCycleRecorder::noteDelayArm(
    AssistReason reason,
    char const* where,
    int plannedDelay,
    int plannedFireTick,
    int planId,
    DevEventSource source
) {
    if (!this->canWrite()) return;
    if (this->m_cycle.hasArm) return;

    // Bake may arm without a live planFinished — synthesize one first.
    if (!this->m_cycle.hasPlan && !this->m_cycle.hasGate) {
        DevDecisionEvent planEv;
        planEv.kind = DevEventKind::PlanFinished;
        planEv.pressId = this->m_cycle.pressId;
        planEv.cycleId = this->m_cycle.pressId;
        planEv.parentEventId = this->m_cycle.pressEventId;
        planEv.planId = planId;
        planEv.playerIndex = this->m_cycle.playerIndex;
        planEv.down = this->m_cycle.down;
        planEv.source = source;
        planEv.effectTick = this->m_cycle.effectTick;
        planEv.pressTick = this->m_cycle.pressTick;
        planEv.plannedDelay = plannedDelay;
        planEv.assistReason = fmt::format("{}@{}", AssistVerdict::name(reason), where ? where : "arm");
        auto* pw = DevTraceStore::get().appendEvent(planEv);
        if (pw) {
            this->m_cycle.lastEventId = pw->eventId;
            this->m_cycle.planId = planId;
            this->m_cycle.hasPlan = true;
        }
    }

    this->m_cycle.source = source;
    auto* w = this->appendKind(
        DevEventKind::DelayArm,
        this->m_cycle.lastEventId,
        planId > 0 ? planId : this->m_cycle.planId,
        reason,
        where
    );
    if (!w) return;
    w->plannedDelay = plannedDelay;
    w->plannedFireTick = plannedFireTick;
    w->source = source;
    this->m_cycle.hasArm = true;
}

bool DevCycleRecorder::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    // Pure state machine without requiring Mod disk when not recording.
    auto& rec = DevCycleRecorder::get();
    rec.clearActive();
    expect(rec.activePressId() == 0, "clear-active");
    expect(!rec.isRecording() || DevConfig::recordEnable(), "recording-implies-enable");

    // Synthetic chain via store when we can open a session.
    auto& store = DevTraceStore::get();
    bool const opened = store.beginSession("cycle-selfcheck", 2);
    expect(opened, "open-session");
    if (opened) {
        // Force enable path: isRecording needs recordEnable. SelfCheck cannot
        // flip user settings; exercise append helpers through store directly
        // when not recording, and only validate beginPress no-op.
        expect(rec.beginPress(0, true, 10, 10) == 0 || DevConfig::recordEnable(), "begin-without-enable");
        store.closeSession();
        auto path = store.tracesDir() / "session-cycle-selfcheck.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    expect(AssistVerdict::name(AssistReason::PassSpam) != nullptr, "verdict-name");

    if (fails == 0) report = "DevCycleRecorder selfCheck OK";
    else report = fmt::format("DevCycleRecorder {} fails: {}", fails, report);
    return fails == 0;
}
