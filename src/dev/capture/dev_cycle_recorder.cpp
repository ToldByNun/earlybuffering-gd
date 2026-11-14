#include "dev_cycle_recorder.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/platform/platform_limits.hpp"
#include "../../debug/lag_probe.hpp"
#include "../dev_config.hpp"
#include "../evaluation/dev_reject_ledger.hpp"
#include "../evaluation/dev_state_diff.hpp"
#include "dev_live_witness.hpp"
#include <Geode/Geode.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>

EbDevCycleRecorder& EbDevCycleRecorder::get() {
    return EbClassManager::get().service<EbDevCycleRecorder>();
}

bool EbDevCycleRecorder::isRecording() const {
    if (!EbDevConfig::recordEnable())
        return false;
    auto& store = EbDevTraceStore::get();
    return store.isOpen() && store.hasOpenAttempt();
}

bool EbDevCycleRecorder::canWriteActive() const {
    return this->isRecording() && this->m_activePressId > 0;
}

void EbDevCycleRecorder::clearActive() {
    this->m_activePressId = 0;
}

EbDevCycleRecorder::TrackedCycle* EbDevCycleRecorder::tracked(int64_t pressId) {
    if (pressId <= 0)
        return nullptr;
    auto it = this->m_cycles.find(pressId);
    if (!(it == this->m_cycles.end())) {
        return &it->second;
    }
    return nullptr;
}

EbDevCycleRecorder::TrackedCycle* EbDevCycleRecorder::ensureActiveTracked() {
    return this->tracked(this->m_activePressId);
}

int64_t EbDevCycleRecorder::beginPress(int playerIndex, bool down, int effectTick, int pressTick,
                                       DevEventSource source) {
    this->clearActive();
    if (!this->isRecording())
        return 0;

    auto& store = EbDevTraceStore::get();
    int64_t const pressId = store.allocPressId();
    if (pressId <= 0)
        return 0;

    EbDevDecisionEvent ev;
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
    if (!written)
        return 0;

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

EbDevDecisionEvent* EbDevCycleRecorder::appendTo(TrackedCycle& cycle, DevEventKind kind,
                                                 int64_t parentEventId, int64_t planId,
                                                 AssistReason reason, char const* where) {
    if (!this->isRecording())
        return nullptr;
    if (parentEventId <= 0 && kind != DevEventKind::PressObserved)
        return nullptr;

    EbDevDecisionEvent ev;
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
        ev.assistReason = EbAssistVerdict::name(reason);
        if (where && where[0]) {
            ev.assistReason = fmt::format("{}@{}", EbAssistVerdict::name(reason), where);
        }
    }
    auto* written = EbDevTraceStore::get().appendEvent(ev);
    if (!written)
        return nullptr;
    cycle.lastEventId = written->eventId;
    if (planId > 0)
        cycle.planId = planId;
    return written;
}

void EbDevCycleRecorder::noteGatePass(AssistReason reason, char const* where) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive())
        return;
    if (cycle->hasPlan || cycle->hasArm || cycle->hasGate)
        return;
    auto* width =
        this->appendTo(*cycle, DevEventKind::GatePass, cycle->lastEventId, 0, reason, where);
    if (!width)
        return;
    // Gate-pass: empty candNotes; attribution is the full reason.
    EbDevRejectLedger::AttributionInput attrIn;
    attrIn.gatePass = true;
    attrIn.hasPlanFinished = false;
    width->attribution = EbDevRejectLedger::buildAttribution(attrIn);
    cycle->hasGate = true;
}

void EbDevCycleRecorder::notePlanFinished(EbSimPlan const& plan, int effectTick, int limit,
                                          EbDevClockContract::Stamp const& clock, PlayLayer* layer,
                                          PlayerObject* player) {
    (void)limit;
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive())
        return;
    if (cycle->hasGate || cycle->hasPlan)
        return;

    auto* width = this->appendTo(
        *cycle, DevEventKind::PlanFinished, cycle->lastEventId, plan.planId,
        plan.reason != AssistReason::None ? plan.reason : AssistReason::PassUnknown, "plan");
    if (!width)
        return;
    width->effectTick = effectTick;
    width->plannedDelay = plan.delay;
    width->hasClockStamp = clock.valid;
    width->clockStamp = clock;
    if (width->hasClockStamp) {
        width->clockStamp.effectTick = effectTick;
    }

    PlayerObject* player2 = nullptr;
    if (layer && layer->m_player2) {
        player2 = layer->m_player2;
    }
    // Commit 12: freeze-world ON while recording (honest counterfactual corpus).
    bool const freezeOnRecord = true;
    width->witness = EbDevLiveWitness::capture(layer, player, player2, plan,
                                               width->hasClockStamp ? width->clockStamp : clock,
                                               effectTick, freezeOnRecord);
    width->hasWitness = width->witness.valid;

    // Commit 8: suppress claims early when the witness alone is notReproducible.
    if (width->hasWitness) {
        EbDevClassifyOptions opts;
        opts.requireDeps = EbDevConfig::evalRequireDeps();
        opts.freezeWorld = freezeOnRecord;
        opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;
        EbDevClassifyResult const gate = EbDevStateDiff::classifyWitnessGate(width->witness, opts);
        if (!gate.evalSuppressedReason.empty()) {
            width->hasFidelity = true;
            width->fidelity = gate.fidelity;
            width->claimsAllowed = gate.claimsAllowed;
            width->approx = gate.approx;
            width->evalSuppressedReason = gate.evalSuppressedReason;
            width->diffs = gate.diffs;
        }
    }

    // Commit 12: full per-delay ledger from plan(); attribution filled later with eval.
    width->candNotes = plan.candNotes;
    width->candNotesTruncated = plan.candNotesTruncated;
    // 1.5.1 AP0: hitch / Adaptive shell cost bits (additive; no plan behavior change).
    width->hasPlanCost = true;
    width->planUs = plan.planUs;
    width->worldCaptureUs = plan.worldCaptureUs;
    width->prepareUs = plan.prepareUs;
    width->guardOriginalDelay = plan.guardOriginalDelay;
    width->guardProbeCount = plan.guardProbeCount;
    width->guardProbeIncomplete = plan.guardProbeIncomplete;

    width->adaptiveWorkUs = plan.adaptiveWorkUs;
    width->fallthroughSetupUs = plan.fallthroughSetupUs;
    width->classicSearchUs = plan.classicSearchUs;
    width->adaptiveSeedCount = plan.adaptiveSeedCount;
    width->reusedCandidateCount = plan.reusedCandidateCount;
    width->reusedCandidateTicks = plan.reusedCandidateTicks;
    width->budgetHit = plan.budgetHit;
    width->candidatesTried = plan.candidatesTried;
    width->adaptiveShell = plan.adaptiveShell;
    width->adaptiveFellThrough = plan.adaptiveFellThrough;
    width->adaptiveAbortedForFallthrough = plan.adaptiveAbortedForFallthrough;
    // Dev Lab AP4: planner branch + resolved ModeParams + hold/threat honesty.
    width->hasPlannerMeta = true;
    width->plannerPath = plan.plannerPath;
    width->inputWindow = plan.fixedFireTick;
    width->adaptiveScreenedCount = plan.adaptiveScreenedCount;
    width->adaptiveSurvivors = plan.adaptiveSurvivors;
    width->adaptiveVerifyGrants = plan.adaptiveVerifyGrants;
    width->adaptiveStolenScreenTicks = plan.adaptiveStolenScreenTicks;
    width->viableDelays = plan.viableDelays;
    width->requireHold = plan.requireHold;
    width->releaseAfter = plan.releaseAfter;
    width->releaseEarliest = plan.releaseEarliest;
    width->releaseLatest = plan.releaseLatest;
    width->idleSafer = plan.idleSafer;
    width->ascentUseful = plan.ascentUseful;
    width->usedOrbWindow = plan.usedOrbWindow;
    width->foresightOrbPad = plan.foresightOrbPad;
    width->longForesightNeedSearch = plan.longForesightNeedSearch;
    width->threatZoneValid = plan.threatZone.valid;
    width->threatZoneStartX = plan.threatZone.startX;
    width->threatZoneEndX = plan.threatZone.endX;
    if (plan.hasWaveMeta) {
        width->hasWaveMeta = true;
        width->waveProxX = plan.waveProxX;
        width->waveNearHazard = plan.waveNearHazard;
        width->waveBestGain = plan.waveBestGain;
        width->waveEarliestLater = plan.waveEarliestLater;
        width->waveHelpOnly = plan.waveHelpOnly;
        width->waveFutile = plan.waveFutile;
        width->waveProximityPass = plan.waveProximityPass;
    }
    if (player) {
        auto const kind = EbStageConfig::kindOf(player);
        width->gameModeKind = EbStageConfig::kindName(kind);
        width->continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
        auto const params = EbStageConfig::paramsFor(kind);
        width->modeTicks = params.ticks;
        width->modeCap = params.cap;
        width->modeSlack = params.slack;
        width->modeLatencyWeight = params.latencyWeight;
        width->modeAdaptivePlan = params.adaptivePlan;
        width->modeAdaptiveScreenTicks = params.adaptiveScreenTicks;
        width->modeAdaptiveVerifyQuantum = params.adaptiveVerifyQuantum;
        width->modeAdaptiveReservePct = params.adaptiveReservePct;
    }
    {
        EbDevRejectLedger::AttributionInput attrIn;
        attrIn.hasPlanFinished = true;
        attrIn.plannedDelay = plan.delay;
        attrIn.notReproducible =
            width->hasFidelity && width->fidelity == DevFidelity::NotReproducible;
        attrIn.approx = width->hasFidelity && width->approx;
        attrIn.claimsAllowed = width->hasFidelity && width->claimsAllowed;
        EbDevRejectLedger::Ledger led;
        led.notes = width->candNotes;
        led.truncated = width->candNotesTruncated;
        attrIn.ledger = &led;
        width->attribution = EbDevRejectLedger::buildAttribution(attrIn);
    }

    cycle->planId = plan.planId;
    cycle->hasPlan = true;
}

void EbDevCycleRecorder::noteLearningOnPlan(int plannedDelay, int finalDelay, bool floored,
                                            bool verifyRefused) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive())
        return;
    if (!cycle->hasPlan)
        return;

    auto* attempt = EbDevTraceStore::get().currentAttempt();
    if (!attempt)
        return;
    EbDevDecisionEvent* planEv = nullptr;
    for (auto& ev : attempt->events) {
        if (ev.pressId == cycle->pressId && ev.kind == DevEventKind::PlanFinished) {
            planEv = &ev;
        }
    }
    if (!planEv)
        return;

    bool const nudged = finalDelay != plannedDelay && !floored && !verifyRefused;
    if (nudged || floored || verifyRefused) {
        EbDevRejectLedger::AttributionInput attrIn;
        attrIn.hasPlanFinished = true;
        attrIn.plannedDelay = plannedDelay;
        attrIn.learningNudged = nudged;
        attrIn.learningFloored = floored;
        attrIn.verifyRefused = verifyRefused;
        attrIn.notReproducible =
            planEv->hasFidelity && planEv->fidelity == DevFidelity::NotReproducible;
        attrIn.approx = planEv->hasFidelity && planEv->approx;
        attrIn.claimsAllowed = planEv->hasFidelity && planEv->claimsAllowed;
        EbDevRejectLedger::Ledger led;
        led.notes = planEv->candNotes;
        led.truncated = planEv->candNotesTruncated;
        attrIn.ledger = &led;
        auto extra = EbDevRejectLedger::buildAttribution(attrIn);
        // Merge without duplicating codes already present.
        for (auto& a : extra) {
            bool dup = false;
            for (auto const& existing : planEv->attribution) {
                if (existing.code == a.code) {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                planEv->attribution.push_back(std::move(a));
        }
        if (nudged && finalDelay >= 0 && finalDelay < static_cast<int>(planEv->candNotes.size())) {
            // Planner chosen stays; mark the final delay as learning-displaced
            // only when it was evaluated and not the planner pick.
            auto& note = planEv->candNotes[static_cast<size_t>(finalDelay)];
            if (note.status != EbDevRejectLedger::Status::Chosen &&
                note.rejectCode != EbDevRejectLedger::RejectCode::LearningDisplaced) {
                if (note.status == EbDevRejectLedger::Status::Evaluated ||
                    note.status == EbDevRejectLedger::Status::Rejected) {
                    note.rejectCode = EbDevRejectLedger::RejectCode::LearningDisplaced;
                    note.detail = EbDevRejectLedger::clampDetail(
                        fmt::format("learn {}→{}", plannedDelay, finalDelay));
                }
            }
        }
    }
}

void EbDevCycleRecorder::notePassThrough(AssistReason reason, char const* where) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive())
        return;
    if (cycle->hasArm || cycle->hasPassThrough)
        return;
    auto* width = this->appendTo(*cycle, DevEventKind::PassThrough, cycle->lastEventId,
                                 cycle->planId, reason, where);
    if (width)
        cycle->hasPassThrough = true;
}

int64_t EbDevCycleRecorder::noteDelayArm(AssistReason reason, char const* where, int plannedDelay,
                                         int plannedFireTick, int planId, DevEventSource source,
                                         DelayArmMeta meta) {
    auto* cycle = this->ensureActiveTracked();
    if (!cycle || !this->canWriteActive())
        return 0;
    if (cycle->hasArm)
        return cycle->pressId;

    if (!cycle->hasPlan && !cycle->hasGate) {
        auto* pw = this->appendTo(*cycle, DevEventKind::PlanFinished, cycle->lastEventId, planId,
                                  reason, where ? where : "arm");
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
    auto* width = this->appendTo(*cycle, DevEventKind::DelayArm, cycle->lastEventId,
                                 planId > 0 ? planId : cycle->planId, reason, where);
    if (!width)
        return 0;
    width->plannedDelay = plannedDelay;
    width->plannedFireTick = plannedFireTick;
    width->source = source;
    width->hasAssistDiag = true;
    width->armVerifyOk = meta.armVerifyOk;
    width->budgetHitAtArm = meta.budgetHitAtArm;
    width->holdExtendMax = meta.holdExtendMax;
    cycle->hasArm = true;
    return cycle->pressId;
}

void EbDevCycleRecorder::noteRecheck(int64_t pressId, int oldFireTick, int newFireTick,
                                     char const* where) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    if (cycle->hasFire || cycle->hasOutcome)
        return;
    if (!cycle->hasArm)
        return;

    auto* width = this->appendTo(*cycle, DevEventKind::Recheck, cycle->lastEventId, cycle->planId,
                                 AssistReason::None, where);
    if (!width)
        return;
    width->plannedFireTick = newFireTick;
    width->actualFireTick = -1;
    int const delta = newFireTick - oldFireTick;
    cycle->recheckDelta += delta;
    width->hasAssistDiag = true;
    width->recheckDelta = delta;
    width->armVerifyOk = cycle->armVerifyOk;
    width->budgetHitAtArm = cycle->budgetHitAtArm;
    width->holdExtendMax = cycle->holdExtendMax;
    width->assistReason =
        fmt::format("recheck:{}→{}@{}", oldFireTick, newFireTick, where ? where : "recheck");
}

void EbDevCycleRecorder::noteHus(int64_t pressId, AssistReason reason, char const* where,
                                 int fireTick) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    if (cycle->hasFire || cycle->hasOutcome)
        return;
    if (!cycle->hasArm)
        return;

    auto* width =
        this->appendTo(*cycle, DevEventKind::Hus, cycle->lastEventId, cycle->planId, reason, where);
    if (!width)
        return;
    width->plannedFireTick = fireTick;
}

void EbDevCycleRecorder::noteFire(int64_t pressId, int actualFireTick) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    if (cycle->hasFire || cycle->hasOutcome)
        return;
    if (!cycle->hasArm)
        return;

    auto* width = this->appendTo(*cycle, DevEventKind::Fire, cycle->lastEventId, cycle->planId,
                                 AssistReason::None, "fire");
    if (!width)
        return;
    width->actualFireTick = actualFireTick;
    width->assistReason = fmt::format("fire@{}", actualFireTick);
    width->hasAssistDiag = true;
    width->armVerifyOk = cycle->armVerifyOk;
    width->budgetHitAtArm = cycle->budgetHitAtArm;
    width->recheckDelta = cycle->recheckDelta;
    width->holdExtendMax = cycle->holdExtendMax;
    if (cycle->releaseSeenTick >= 0 || cycle->holdUserUpTick >= 0 || cycle->holdSwallow ||
        cycle->holdInjectReleaseTick >= 0) {
        width->hasHoldTimeline = true;
        width->releaseSeenTick = cycle->releaseSeenTick;
        width->holdUserUpTick = cycle->holdUserUpTick;
        width->holdInjectReleaseTick = cycle->holdInjectReleaseTick;
        width->holdSwallow = cycle->holdSwallow;
    }
    cycle->actualFireTick = actualFireTick;
    cycle->hasFire = true;
}

void EbDevCycleRecorder::noteHoldExtend(int64_t pressId, int holdExtendMax) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    if (holdExtendMax < 0)
        return;
    cycle->holdExtendMax = holdExtendMax;
}

void EbDevCycleRecorder::noteReleaseSeen(int64_t pressId, int attemptTick) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    if (cycle->releaseSeenTick < 0)
        cycle->releaseSeenTick = attemptTick;
}

void EbDevCycleRecorder::noteHoldSwallow(int64_t pressId, int attemptTick) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    cycle->holdSwallow = true;
    if (cycle->holdUserUpTick < 0)
        cycle->holdUserUpTick = attemptTick;
}

void EbDevCycleRecorder::noteHoldInjectRelease(int64_t pressId, int attemptTick) {
    auto* cycle = this->tracked(pressId);
    if (!cycle || !this->isRecording())
        return;
    cycle->holdInjectReleaseTick = attemptTick;
    // Stamp onto the latest fire event if already written.
    auto* attempt = EbDevTraceStore::get().currentAttempt();
    if (!attempt)
        return;
    for (auto it = attempt->events.rbegin(); it != attempt->events.rend(); ++it) {
        if (it->pressId != pressId)
            continue;
        if (it->kind == DevEventKind::Fire || it->kind == DevEventKind::Outcome) {
            it->hasHoldTimeline = true;
            it->releaseSeenTick = cycle->releaseSeenTick;
            it->holdUserUpTick = cycle->holdUserUpTick;
            it->holdInjectReleaseTick = attemptTick;
            it->holdSwallow = cycle->holdSwallow;
            if (it->kind == DevEventKind::Fire)
                break;
        }
    }
}

void EbDevCycleRecorder::resetModeTimeline() {
    this->m_modeTimeline = {};
}

void EbDevCycleRecorder::fillTimelineMeta(EbDevDecisionEvent& ev, PlayLayer* layer,
                                          PlayerObject* player, char const* change,
                                          char const* fromKind) const {
    ev.hasTimelineMeta = true;
    ev.timelineChange = change ? change : "";
    if (fromKind && fromKind[0] != '\0')
        ev.fromGameModeKind = fromKind;
    if (!player)
        return;
    auto const kind = EbStageConfig::kindOf(player);
    ev.gameModeKind = EbStageConfig::kindName(kind);
    ev.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
    ev.playerSpeed = player->m_playerSpeed;
    ev.upsideDown = player->m_isUpsideDown;
    ev.playerX = player->getPositionX();
    ev.dual = layer && layer->m_gameState.m_isDualMode;
}

EbDevDecisionEvent* EbDevCycleRecorder::appendAttemptScoped(DevEventKind kind, int attemptTick,
                                                            char const* reason) {
    if (!this->isRecording())
        return nullptr;
    EbDevDecisionEvent ev;
    ev.kind = kind;
    ev.pressId = 0;
    ev.cycleId = 0;
    ev.parentEventId = 0;
    ev.effectTick = attemptTick;
    ev.pressTick = attemptTick;
    if (reason)
        ev.assistReason = reason;
    return EbDevTraceStore::get().appendEvent(ev);
}

void EbDevCycleRecorder::noteCheckpoint(PlayLayer* layer, int attemptTick) {
    auto* written = this->appendAttemptScoped(DevEventKind::Checkpoint, attemptTick, "checkpoint");
    if (!written)
        return;
    PlayerObject* player = layer ? layer->m_player1 : nullptr;
    char const* fromKind = nullptr;
    if (this->m_modeTimeline.known)
        fromKind = EbStageConfig::kindName(this->m_modeTimeline.mode);
    this->fillTimelineMeta(*written, layer, player, "checkpoint", fromKind);
    if (player) {
        this->m_modeTimeline.known = true;
        this->m_modeTimeline.mode = EbStageConfig::kindOf(player);
        this->m_modeTimeline.playerSpeed = player->m_playerSpeed;
        this->m_modeTimeline.upsideDown = player->m_isUpsideDown;
        this->m_modeTimeline.dual = layer && layer->m_gameState.m_isDualMode;
    }
}

void EbDevCycleRecorder::pollModeTimeline(PlayLayer* layer, int attemptTick) {
    if (!this->isRecording() || !layer)
        return;
    auto* player = layer->m_player1;
    if (!player || player->m_isDead)
        return;

    auto const kind = EbStageConfig::kindOf(player);
    float const speed = player->m_playerSpeed;
    bool const upsideDown = player->m_isUpsideDown;
    bool const dual = layer->m_gameState.m_isDualMode;

    if (!this->m_modeTimeline.known) {
        auto* written =
            this->appendAttemptScoped(DevEventKind::ModeTransition, attemptTick, "timeline-seed");
        if (written)
            this->fillTimelineMeta(*written, layer, player, "seed", nullptr);
        this->m_modeTimeline.known = true;
        this->m_modeTimeline.mode = kind;
        this->m_modeTimeline.playerSpeed = speed;
        this->m_modeTimeline.upsideDown = upsideDown;
        this->m_modeTimeline.dual = dual;
        return;
    }

    if (kind != this->m_modeTimeline.mode) {
        char const* fromKind = EbStageConfig::kindName(this->m_modeTimeline.mode);
        auto* written =
            this->appendAttemptScoped(DevEventKind::ModeTransition, attemptTick, "timeline-mode");
        if (written)
            this->fillTimelineMeta(*written, layer, player, "mode", fromKind);
        this->m_modeTimeline.mode = kind;
    }
    if (std::fabs(speed - this->m_modeTimeline.playerSpeed) > 1e-3f) {
        auto* written =
            this->appendAttemptScoped(DevEventKind::ModeTransition, attemptTick, "timeline-speed");
        if (written)
            this->fillTimelineMeta(*written, layer, player, "speed",
                                   EbStageConfig::kindName(this->m_modeTimeline.mode));
        this->m_modeTimeline.playerSpeed = speed;
    }
    if (upsideDown != this->m_modeTimeline.upsideDown) {
        auto* written = this->appendAttemptScoped(DevEventKind::ModeTransition, attemptTick,
                                                  "timeline-gravity");
        if (written)
            this->fillTimelineMeta(*written, layer, player, "gravity",
                                   EbStageConfig::kindName(this->m_modeTimeline.mode));
        this->m_modeTimeline.upsideDown = upsideDown;
    }
    if (dual != this->m_modeTimeline.dual) {
        auto* written =
            this->appendAttemptScoped(DevEventKind::ModeTransition, attemptTick, "timeline-dual");
        if (written)
            this->fillTimelineMeta(*written, layer, player, "dual",
                                   EbStageConfig::kindName(this->m_modeTimeline.mode));
        this->m_modeTimeline.dual = dual;
    }
}

void EbDevCycleRecorder::writeOutcomesForOpenCycles(std::string const& endReason, bool lived,
                                                    int attemptTick) {
    if (!this->isRecording())
        return;
    for (auto& pair : this->m_cycles) {
        auto& cycle = pair.second;
        if (cycle.hasOutcome)
            continue;
        if (cycle.lastEventId <= 0)
            continue;

        // Armed but never injected (death mid-buffer): synthetic fire so
        // outcome parent stays fire|passThrough|gatePass for chain validation.
        if (cycle.hasArm && !cycle.hasFire) {
            auto* fw = this->appendTo(cycle, DevEventKind::Fire, cycle.lastEventId, cycle.planId,
                                      AssistReason::None, "cancelled");
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

        auto* width = this->appendTo(cycle, DevEventKind::Outcome, cycle.lastEventId, cycle.planId,
                                     AssistReason::None, endReason.c_str());
        if (!width)
            continue;
        width->endReason = endReason;
        width->assistReason = fmt::format("outcome:{}:{}", endReason, lived ? "lived" : "died");
        width->hasAssistDiag = true;
        width->armVerifyOk = cycle.armVerifyOk;
        width->budgetHitAtArm = cycle.budgetHitAtArm;
        width->recheckDelta = cycle.recheckDelta;
        width->holdExtendMax = cycle.holdExtendMax;
        if (cycle.releaseSeenTick >= 0 || cycle.holdUserUpTick >= 0 || cycle.holdSwallow ||
            cycle.holdInjectReleaseTick >= 0) {
            width->hasHoldTimeline = true;
            width->releaseSeenTick = cycle.releaseSeenTick;
            width->holdUserUpTick = cycle.holdUserUpTick;
            width->holdInjectReleaseTick = cycle.holdInjectReleaseTick;
            width->holdSwallow = cycle.holdSwallow;
        }
        if (!lived && cycle.hasArm && cycle.actualFireTick >= 0 && attemptTick >= 0) {
            width->deathAfterFire = true;
            width->ticksToDeath = std::max(0, attemptTick - cycle.actualFireTick);
            EbLagProbe::get().mark(
                "eb.assist.halluc",
                fmt::format("press={} fire={} deathTick={} ticksToDeath={} recheckΔ={} holdExt={}",
                            cycle.pressId, cycle.actualFireTick, attemptTick, width->ticksToDeath,
                            cycle.recheckDelta, cycle.holdExtendMax));
        }
        cycle.hasOutcome = true;
    }
}

bool EbDevCycleRecorder::shouldCountAttempt() const {
    if (EbDevConfig::countEmptyAttempts())
        return true;
    for (auto const& pair : this->m_cycles) {
        if (pair.second.pressId > 0)
            return true;
    }
    // Mode-timeline-only attempts must not consume the rolling window.
    auto const* attempt = EbDevTraceStore::get().currentAttempt();
    if (!attempt)
        return false;
    for (auto const& ev : attempt->events) {
        if (ev.kind == DevEventKind::PressObserved)
            return true;
    }
    return false;
}

bool EbDevCycleRecorder::sealAttemptWithOutcomes(std::string endReason, bool lived,
                                                 int attemptTick) {
    if (!EbDevConfig::recordEnable())
        return false;
    auto& store = EbDevTraceStore::get();
    if (!store.isOpen() || !store.hasOpenAttempt())
        return false;

    this->writeOutcomesForOpenCycles(endReason, lived, attemptTick);
    bool const count = this->shouldCountAttempt();
    this->m_cycles.clear();
    this->m_activePressId = 0;
    this->resetModeTimeline();
    return store.sealAttempt(std::move(endReason), count);
}

bool EbDevCycleRecorder::beginNewAttempt(int startTick) {
    if (!EbDevConfig::recordEnable())
        return false;
    auto& store = EbDevTraceStore::get();
    if (!store.isOpen())
        return false;
    if (store.hasOpenAttempt())
        return true;
    this->m_cycles.clear();
    this->m_activePressId = 0;
    this->resetModeTimeline();
    return store.beginAttempt(startTick);
}

void EbDevCycleRecorder::onAttemptReset(int startTick) {
    if (!EbDevConfig::recordEnable())
        return;
    auto& store = EbDevTraceStore::get();
    if (!store.isOpen())
        return;
    if (store.hasOpenAttempt()) {
        (void)this->sealAttemptWithOutcomes("reset", false, startTick);
    }
    (void)this->beginNewAttempt(startTick);
}

bool EbDevCycleRecorder::flushQuit() {
    if (!EbDevConfig::recordEnable())
        return false;
    auto& store = EbDevTraceStore::get();
    if (!store.isOpen())
        return false;
    if (store.hasOpenAttempt()) {
        this->writeOutcomesForOpenCycles("quit", false, -1);
        bool const count = this->shouldCountAttempt();
        this->m_cycles.clear();
        this->m_activePressId = 0;
        this->resetModeTimeline();
        (void)store.sealAttempt("quit", count);
    }
    return store.flush();
}

bool EbDevCycleRecorder::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    auto& rec = EbDevCycleRecorder::get();
    rec.clearActive();
    rec.m_cycles.clear();
    expect(rec.activePressId() == 0, "clear-active");

    // Disk session open is skipped on constrained devices (Android/iOS).
    if (EbPlatformLimits::get().allowLoadDiskSelfCheck()) {
        auto& store = EbDevTraceStore::get();
        bool const opened = store.beginSession("cycle-selfcheck5", 2);
        expect(opened, "open-session");
        if (opened) {
            expect(store.beginAttempt(0), "begin-attempt");
            // Without recordEnable, beginPress no-ops — still exercise store chain.
            expect(rec.beginPress(0, true, 10, 10) == 0 || EbDevConfig::recordEnable(),
                   "begin-gate");
            store.closeSession();
            auto path = store.tracesDir() / "session-cycle-selfcheck5.json";
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }

    expect(EbAssistVerdict::name(AssistReason::PassSpam) != nullptr, "verdict-name");

    if (fails == 0)
        report = "DevCycleRecorder selfCheck OK";
    else
        report = fmt::format("DevCycleRecorder {} fails: {}", fails, report);
    return fails == 0;
}
