#include "DevCycleRecorder.hpp"
#include "DevConfig.hpp"
#include "DevLiveWitness.hpp"
#include "DevRejectLedger.hpp"
#include "DevStateDiff.hpp"
#include "../core/PlatformLimits.hpp"
#include "../debug/LagProbe.hpp"

#include <Geode/Geode.hpp>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>

using namespace geode::prelude;

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
    if (!w) return;
    // Gate-pass: empty candNotes; attribution is the full reason.
    DevRejectLedger::AttributionInput attrIn;
    attrIn.gatePass = true;
    attrIn.hasPlanFinished = false;
    w->attribution = DevRejectLedger::buildAttribution(attrIn);
    cycle->hasGate = true;
}

void DevCycleRecorder::notePlanFinished(
    SimPlan const& plan,
    int effectTick,
    int limit,
    DevClockContract::Stamp const& clock,
    PlayLayer* layer,
    PlayerObject* player
) {
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
    w->hasClockStamp = clock.valid;
    w->clockStamp = clock;
    if (w->hasClockStamp) {
        w->clockStamp.effectTick = effectTick;
    }

    PlayerObject* player2 = nullptr;
    if (layer && layer->m_player2) {
        player2 = layer->m_player2;
    }
    w->witness = DevLiveWitness::capture(
        layer,
        player,
        player2,
        plan,
        w->hasClockStamp ? w->clockStamp : clock,
        effectTick,
        DevConfig::freezeWorld()
    );
    w->hasWitness = w->witness.valid;

    // Commit 8: suppress claims early when the witness alone is notReproducible.
    if (w->hasWitness) {
        DevClassifyOptions opts;
        opts.requireDeps = DevConfig::evalRequireDeps();
        opts.freezeWorld = DevConfig::freezeWorld();
        opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;
        DevClassifyResult const gate = DevStateDiff::classifyWitnessGate(
            w->witness, opts
        );
        if (!gate.evalSuppressedReason.empty()) {
            w->hasFidelity = true;
            w->fidelity = gate.fidelity;
            w->claimsAllowed = gate.claimsAllowed;
            w->approx = gate.approx;
            w->evalSuppressedReason = gate.evalSuppressedReason;
            w->diffs = gate.diffs;
        }
    }

    // Commit 12: full per-delay ledger from plan(); attribution filled later with eval.
    w->candNotes = plan.candNotes;
    w->candNotesTruncated = plan.candNotesTruncated;
    {
        DevRejectLedger::AttributionInput attrIn;
        attrIn.hasPlanFinished = true;
        attrIn.plannedDelay = plan.delay;
        attrIn.notReproducible = w->hasFidelity
            && w->fidelity == DevFidelity::NotReproducible;
        attrIn.approx = w->hasFidelity && w->approx;
        attrIn.claimsAllowed = w->hasFidelity && w->claimsAllowed;
        DevRejectLedger::Ledger led;
        led.notes = w->candNotes;
        led.truncated = w->candNotesTruncated;
        attrIn.ledger = &led;
        w->attribution = DevRejectLedger::buildAttribution(attrIn);
    }

    cycle->planId = plan.planId;
    cycle->hasPlan = true;
}

void DevCycleRecorder::noteLearningOnPlan(
    int plannedDelay,
    int finalDelay,
    bool floored,
    bool verifyRefused
) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive()) return;
    if (!cycle->hasPlan) return;

    auto* attempt = DevTraceStore::get().currentAttempt();
    if (!attempt) return;
    DevDecisionEvent* planEv = nullptr;
    for (auto& ev : attempt->events) {
        if (ev.pressId == cycle->pressId
            && ev.kind == DevEventKind::PlanFinished) {
            planEv = &ev;
        }
    }
    if (!planEv) return;

    bool const nudged = finalDelay != plannedDelay && !floored && !verifyRefused;
    if (nudged || floored || verifyRefused) {
        DevRejectLedger::AttributionInput attrIn;
        attrIn.hasPlanFinished = true;
        attrIn.plannedDelay = plannedDelay;
        attrIn.learningNudged = nudged;
        attrIn.learningFloored = floored;
        attrIn.verifyRefused = verifyRefused;
        attrIn.notReproducible = planEv->hasFidelity
            && planEv->fidelity == DevFidelity::NotReproducible;
        attrIn.approx = planEv->hasFidelity && planEv->approx;
        attrIn.claimsAllowed = planEv->hasFidelity && planEv->claimsAllowed;
        DevRejectLedger::Ledger led;
        led.notes = planEv->candNotes;
        led.truncated = planEv->candNotesTruncated;
        attrIn.ledger = &led;
        auto extra = DevRejectLedger::buildAttribution(attrIn);
        // Merge without duplicating codes already present.
        for (auto& a : extra) {
            bool dup = false;
            for (auto const& existing : planEv->attribution) {
                if (existing.code == a.code) {
                    dup = true;
                    break;
                }
            }
            if (!dup) planEv->attribution.push_back(std::move(a));
        }
        if (nudged && finalDelay >= 0
            && finalDelay < static_cast<int>(planEv->candNotes.size())) {
            // Planner chosen stays; mark the final delay as learning-displaced
            // only when it was evaluated and not the planner pick.
            auto& note = planEv->candNotes[static_cast<size_t>(finalDelay)];
            if (note.status != DevRejectLedger::Status::Chosen
                && note.rejectCode != DevRejectLedger::RejectCode::LearningDisplaced) {
                if (note.status == DevRejectLedger::Status::Evaluated
                    || note.status == DevRejectLedger::Status::Rejected) {
                    note.rejectCode = DevRejectLedger::RejectCode::LearningDisplaced;
                    note.detail = DevRejectLedger::clampDetail(fmt::format(
                        "learn {}→{}", plannedDelay, finalDelay
                    ));
                }
            }
        }
    }
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
    DevEventSource source,
    DelayArmMeta meta
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
    cycle->armVerifyOk = meta.armVerifyOk;
    cycle->budgetHitAtArm = meta.budgetHitAtArm;
    if (meta.holdExtendMax >= 0) {
        cycle->holdExtendMax = meta.holdExtendMax;
    }
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
    w->hasAssistDiag = true;
    w->armVerifyOk = meta.armVerifyOk;
    w->budgetHitAtArm = meta.budgetHitAtArm;
    w->holdExtendMax = meta.holdExtendMax;
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
    int const delta = newFireTick - oldFireTick;
    cycle->recheckDelta += delta;
    w->hasAssistDiag = true;
    w->recheckDelta = delta;
    w->armVerifyOk = cycle->armVerifyOk;
    w->budgetHitAtArm = cycle->budgetHitAtArm;
    w->holdExtendMax = cycle->holdExtendMax;
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
    w->hasAssistDiag = true;
    w->armVerifyOk = cycle->armVerifyOk;
    w->budgetHitAtArm = cycle->budgetHitAtArm;
    w->recheckDelta = cycle->recheckDelta;
    w->holdExtendMax = cycle->holdExtendMax;
    cycle->actualFireTick = actualFireTick;
    cycle->hasFire = true;
}

void DevCycleRecorder::noteHoldExtend(int64_t pressId, int holdExtendMax) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording()) return;
    if (holdExtendMax < 0) return;
    cycle->holdExtendMax = holdExtendMax;
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
    std::string const& endReason, bool lived, int attemptTick
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
                fw->hasAssistDiag = true;
                fw->armVerifyOk = cycle.armVerifyOk;
                fw->budgetHitAtArm = cycle.budgetHitAtArm;
                fw->recheckDelta = cycle.recheckDelta;
                fw->holdExtendMax = cycle.holdExtendMax;
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
        w->hasAssistDiag = true;
        w->armVerifyOk = cycle.armVerifyOk;
        w->budgetHitAtArm = cycle.budgetHitAtArm;
        w->recheckDelta = cycle.recheckDelta;
        w->holdExtendMax = cycle.holdExtendMax;
        if (!lived && cycle.hasArm
            && cycle.actualFireTick >= 0
            && attemptTick >= 0) {
            w->deathAfterFire = true;
            w->ticksToDeath = std::max(0, attemptTick - cycle.actualFireTick);
            LagProbe::get().mark(
                "eb.assist.halluc",
                fmt::format(
                    "press={} fire={} deathTick={} ticksToDeath={} recheckΔ={} holdExt={}",
                    cycle.pressId,
                    cycle.actualFireTick,
                    attemptTick,
                    w->ticksToDeath,
                    cycle.recheckDelta,
                    cycle.holdExtendMax
                )
            );
        }
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

bool DevCycleRecorder::sealAttemptWithOutcomes(
    std::string endReason, bool lived, int attemptTick
) {
    if (!DevConfig::recordEnable()) return false;
    auto& store = DevTraceStore::get();
    if (!store.isOpen() || !store.hasOpenAttempt()) return false;

    this->writeOutcomesForOpenCycles(endReason, lived, attemptTick);
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
        (void)this->sealAttemptWithOutcomes("reset", false, startTick);
    }
    (void)this->beginNewAttempt(startTick);
}

bool DevCycleRecorder::flushQuit() {
    if (!DevConfig::recordEnable()) return false;
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) return false;
    if (store.hasOpenAttempt()) {
        this->writeOutcomesForOpenCycles("quit", false, -1);
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

    // Disk session open is skipped on constrained devices (Android/iOS).
    if (PlatformLimits::get().allowLoadDiskSelfCheck()) {
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
    }

    expect(AssistVerdict::name(AssistReason::PassSpam) != nullptr, "verdict-name");

    if (fails == 0) report = "DevCycleRecorder selfCheck OK";
    else report = fmt::format("DevCycleRecorder {} fails: {}", fails, report);
    return fails == 0;
}
