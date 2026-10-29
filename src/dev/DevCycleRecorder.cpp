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

bool DevCycleRecorder::canWriteActive() const {
    return this->isRecording() && this->m_activePressId > 0;
}

void DevCycleRecorder::clearActive() {
    this->m_activePressId = 0;
}

DevCycleRecorder::TrackedCycle* DevCycleRecorder::tracked(int64_t pressId) {
    if (pressId <= 0) return nullptr;
    auto it = this->m_cycles.find(pressId);
    if (it == this->m_cycles.end()) return nullptr;
    return &it->second;
}

DevCycleRecorder::TrackedCycle* DevCycleRecorder::ensureActiveTracked() {
    return this->tracked(this->m_activePressId);
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

    TrackedCycle cycle;
    cycle.pressId = pressId;
    cycle.lastEventId = written->eventId;
    cycle.playerIndex = playerIndex;
    cycle.down = down;
    cycle.source = source;
    cycle.effectTick = effectTick;
    cycle.pressTick = pressTick;
    this->m_cycles[pressId] = cycle;
    this->m_activePressId = pressId;
    return pressId;
}

DevDecisionEvent* DevCycleRecorder::appendTo(
    TrackedCycle& cycle,
    DevEventKind kind,
    int64_t parentEventId,
    int64_t planId,
    AssistReason reason,
    char const* where
) {
    if (!this->isRecording()) return nullptr;
    if (parentEventId <= 0 && kind != DevEventKind::PressObserved) return nullptr;

    DevDecisionEvent ev;
    ev.kind = kind;
    ev.pressId = cycle.pressId;
    ev.cycleId = cycle.pressId;
    ev.parentEventId = parentEventId;
    ev.planId = planId;
    ev.playerIndex = cycle.playerIndex;
    ev.down = cycle.down;
    ev.source = cycle.source;
    ev.effectTick = cycle.effectTick;
    ev.pressTick = cycle.pressTick;
    if (reason != AssistReason::None || (where && where[0])) {
        ev.assistReason = AssistVerdict::name(reason);
        if (where && where[0]) {
            ev.assistReason = fmt::format(
                "{}@{}", AssistVerdict::name(reason), where
            );
        }
    }
    auto* written = DevTraceStore::get().appendEvent(ev);
    if (!written) return nullptr;
    cycle.lastEventId = written->eventId;
    if (planId > 0) cycle.planId = planId;
    return written;
}

void DevCycleRecorder::noteGatePass(AssistReason reason, char const* where) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive()) return;
    if (cycle->hasPlan || cycle->hasArm || cycle->hasGate) return;
    auto* w = this->appendTo(
        *cycle, DevEventKind::GatePass, cycle->lastEventId, 0, reason, where
    );
    if (w) cycle->hasGate = true;
}

void DevCycleRecorder::notePlanFinished(SimPlan const& plan, int effectTick, int limit) {
    (void)limit;
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive()) return;
    if (cycle->hasGate || cycle->hasPlan) return;

    auto* w = this->appendTo(
        *cycle,
        DevEventKind::PlanFinished,
        cycle->lastEventId,
        plan.planId,
        plan.reason != AssistReason::None ? plan.reason : AssistReason::PassUnknown,
        "plan"
    );
    if (!w) return;
    w->effectTick = effectTick;
    w->plannedDelay = plan.delay;
    cycle->planId = plan.planId;
    cycle->hasPlan = true;
}

void DevCycleRecorder::notePassThrough(AssistReason reason, char const* where) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive()) return;
    if (cycle->hasArm || cycle->hasPassThrough) return;
    auto* w = this->appendTo(
        *cycle,
        DevEventKind::PassThrough,
        cycle->lastEventId,
        cycle->planId,
        reason,
        where
    );
    if (w) cycle->hasPassThrough = true;
}

int64_t DevCycleRecorder::noteDelayArm(
    AssistReason reason,
    char const* where,
    int plannedDelay,
    int plannedFireTick,
    int planId,
    DevEventSource source
) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive()) return 0;
    if (cycle->hasArm) return cycle->pressId;

    if (!cycle->hasPlan && !cycle->hasGate) {
        auto* pw = this->appendTo(
            *cycle,
            DevEventKind::PlanFinished,
            cycle->lastEventId,
            planId,
            reason,
            where ? where : "arm"
        );
        if (pw) {
            pw->plannedDelay = plannedDelay;
            pw->source = source;
            cycle->hasPlan = true;
            cycle->planId = planId;
        }
    }

    cycle->source = source;
    auto* w = this->appendTo(
        *cycle,
        DevEventKind::DelayArm,
        cycle->lastEventId,
        planId > 0 ? planId : cycle->planId,
        reason,
        where
    );
    if (!w) return 0;
    w->plannedDelay = plannedDelay;
    w->plannedFireTick = plannedFireTick;
    w->source = source;
    cycle->hasArm = true;
    return cycle->pressId;
}

void DevCycleRecorder::noteRecheck(
    int64_t pressId,
    int oldFireTick,
    int newFireTick,
    char const* where
) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording()) return;
    if (cycle->hasFire || cycle->hasOutcome) return;
    if (!cycle->hasArm) return;

    auto* w = this->appendTo(
        *cycle,
        DevEventKind::Recheck,
        cycle->lastEventId,
        cycle->planId,
        AssistReason::None,
        where
    );
    if (!w) return;
    w->plannedFireTick = newFireTick;
    w->actualFireTick = -1;
    w->assistReason = fmt::format(
        "recheck:{}→{}@{}",
        oldFireTick,
        newFireTick,
        where ? where : "recheck"
    );
}

void DevCycleRecorder::noteHus(
    int64_t pressId,
    AssistReason reason,
    char const* where,
    int fireTick
) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording()) return;
    if (cycle->hasFire || cycle->hasOutcome) return;
    if (!cycle->hasArm) return;

    auto* w = this->appendTo(
        *cycle,
        DevEventKind::Hus,
        cycle->lastEventId,
        cycle->planId,
        reason,
        where
    );
    if (!w) return;
    w->plannedFireTick = fireTick;
}

void DevCycleRecorder::noteFire(int64_t pressId, int actualFireTick) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording()) return;
    if (cycle->hasFire || cycle->hasOutcome) return;
    if (!cycle->hasArm) return;

    auto* w = this->appendTo(
        *cycle,
        DevEventKind::Fire,
        cycle->lastEventId,
        cycle->planId,
        AssistReason::None,
        "fire"
    );
    if (!w) return;
    w->actualFireTick = actualFireTick;
    w->assistReason = fmt::format("fire@{}", actualFireTick);
    cycle->hasFire = true;
}

void DevCycleRecorder::noteCheckpoint(int attemptTick) {
    if (!this->isRecording()) return;
    DevDecisionEvent ev;
    ev.kind = DevEventKind::Checkpoint;
    ev.pressId = 0;
    ev.cycleId = 0;
    ev.parentEventId = 0;
    ev.effectTick = attemptTick;
    ev.pressTick = attemptTick;
    ev.assistReason = "checkpoint";
    (void)DevTraceStore::get().appendEvent(ev);
}

void DevCycleRecorder::writeOutcomesForOpenCycles(
    std::string const& endReason, bool lived
) {
    if (!this->isRecording()) return;
    for (auto& pair : this->m_cycles) {
        auto& cycle = pair.second;
        if (cycle.hasOutcome) continue;
        if (cycle.lastEventId <= 0) continue;

        // Armed but never injected (death mid-buffer): synthetic fire so
        // outcome parent stays fire|passThrough|gatePass for chain validation.
        if (cycle.hasArm && !cycle.hasFire) {
            auto* fw = this->appendTo(
                cycle,
                DevEventKind::Fire,
                cycle.lastEventId,
                cycle.planId,
                AssistReason::None,
                "cancelled"
            );
            if (fw) {
                fw->actualFireTick = -1;
                fw->assistReason = fmt::format("fire-cancelled@{}", endReason);
                cycle.hasFire = true;
            }
        }

        auto* w = this->appendTo(
            cycle,
            DevEventKind::Outcome,
            cycle.lastEventId,
            cycle.planId,
            AssistReason::None,
            endReason.c_str()
        );
        if (!w) continue;
        w->endReason = endReason;
        w->assistReason = fmt::format(
            "outcome:{}:{}",
            endReason,
            lived ? "lived" : "died"
        );
        cycle.hasOutcome = true;
    }
}

bool DevCycleRecorder::shouldCountAttempt() const {
    if (DevConfig::countEmptyAttempts()) return true;
    for (auto const& pair : this->m_cycles) {
        if (pair.second.pressId > 0) return true;
    }
    auto const* attempt = DevTraceStore::get().currentAttempt();
    if (!attempt) return false;
    return !attempt->events.empty();
}

bool DevCycleRecorder::sealAttemptWithOutcomes(std::string endReason, bool lived) {
    if (!DevConfig::recordEnable()) return false;
    auto& store = DevTraceStore::get();
    if (!store.isOpen() || !store.hasOpenAttempt()) return false;

    this->writeOutcomesForOpenCycles(endReason, lived);
    bool const count = this->shouldCountAttempt();
    this->m_cycles.clear();
    this->m_activePressId = 0;
    return store.sealAttempt(std::move(endReason), count);
}

bool DevCycleRecorder::beginNewAttempt(int startTick) {
    if (!DevConfig::recordEnable()) return false;
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) return false;
    if (store.hasOpenAttempt()) return true;
    this->m_cycles.clear();
    this->m_activePressId = 0;
    return store.beginAttempt(startTick);
}

void DevCycleRecorder::onAttemptReset(int startTick) {
    if (!DevConfig::recordEnable()) return;
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) return;
    if (store.hasOpenAttempt()) {
        (void)this->sealAttemptWithOutcomes("reset", false);
    }
    (void)this->beginNewAttempt(startTick);
}

bool DevCycleRecorder::flushQuit() {
    if (!DevConfig::recordEnable()) return false;
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) return false;
    if (store.hasOpenAttempt()) {
        this->writeOutcomesForOpenCycles("quit", false);
        bool const count = this->shouldCountAttempt();
        this->m_cycles.clear();
        this->m_activePressId = 0;
        (void)store.sealAttempt("quit", count);
    }
    return store.flush();
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

    auto& rec = DevCycleRecorder::get();
    rec.clearActive();
    rec.m_cycles.clear();
    expect(rec.activePressId() == 0, "clear-active");

    auto& store = DevTraceStore::get();
    bool const opened = store.beginSession("cycle-selfcheck5", 2);
    expect(opened, "open-session");
    if (opened) {
        expect(store.beginAttempt(0), "begin-attempt");
        // Without recordEnable, beginPress no-ops — still exercise store chain.
        expect(rec.beginPress(0, true, 10, 10) == 0 || DevConfig::recordEnable(), "begin-gate");
        store.closeSession();
        auto path = store.tracesDir() / "session-cycle-selfcheck5.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    expect(AssistVerdict::name(AssistReason::PassSpam) != nullptr, "verdict-name");

    if (fails == 0) report = "DevCycleRecorder selfCheck OK";
    else report = fmt::format("DevCycleRecorder {} fails: {}", fails, report);
    return fails == 0;
}
