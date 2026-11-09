#include "assist_outcomes.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../dev/dev_config.hpp"
#include "../../../dev/reporting/dev_report.hpp"
#include "../../../ui/feedback/save_frame_counter.hpp"
#include "../../bake/hybrid_verify_cache.hpp"
#include "../../diagnostics/assist_real_metrics.hpp"
#include "../../diagnostics/decision_log.hpp"
#include "../../diagnostics/divergence_tracker.hpp"
#include "../../learning/level_profile.hpp"
#include "../../sim/planning/simulation_plan.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../assist_controller.hpp"
#include "../contracts/assist_verdict.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

void EbAssistOutcomes::noteRealJump(EbAssistController& controller, PlayerObject* player) {
    if (controller.m_buffer.state != BufferState::FiredHolding)
        return;
    auto* playLayer = PlayLayer::get();
    if (!playLayer)
        return;
    auto* ours = controller.m_buffer.player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (player == ours)
        controller.m_buffer.jumpSeen = true;
}

void EbAssistOutcomes::recordDecision(EbAssistController& controller, PlayLayer* playLayer,
                                    PlayerObject* player, bool down, EbSimPlan const& plan,
                                    int effectTick, int appliedDelay, int limit) {
    if (!playLayer || !player)
        return;

    EbDecisionEntry e;
    e.tick = effectTick;
    e.x = player->getPositionX();
    e.mode = EbStageConfig::kindOf(player);
    e.down = down;
    e.delay = appliedDelay;
    e.limit = limit;
    e.firstDeathTick = plan.firstDeathTick;
    e.action = appliedDelay > 0 ? PlanAction::Delay : plan.action;
    e.nowIsSafe = plan.nowIsSafe;
    e.assisted = appliedDelay > 0;
    e.horizon = EbShadowSimulator::modelFor(player, limit, EbLevelProfile::get().capMul(e.mode)).cap;
    e.snapshotSlot = EbShadowSimulator::get().captureSnapshot(player);
    // Seed timeline with the planned action at fire tick.
    int const fire = effectTick + appliedDelay;
    e.timeline.push_back(EbInputEvent{fire, down});
    controller.m_log.push(std::move(e));
}

void EbAssistOutcomes::tickLearning(EbAssistController& controller, PlayLayer* playLayer,
                                  PlayerObject* player) {
    if (!EbStageConfig::autoTune() || !playLayer || !player)
        return;
    auto& profile = EbLevelProfile::get();
    if (!profile.loaded())
        return;

    for (auto& e : controller.m_log.entries()) {
        if (e.resolved)
            continue;
        int const age = controller.m_attemptTick - e.tick;
        if (age < 0)
            continue;

        // Pessimism: Pass with predicted death, but player still alive past that.
        if (!e.assisted && e.firstDeathTick >= 0 && age >= e.firstDeathTick + 4) {
            profile.notePessimism(e.mode, e.x);
            e.resolved = true;
            continue;
        }

        // Survival past horizon and past the spot's X.
        if (e.assisted && age >= e.horizon) {
            float const playerX = player->getPositionX();
            if (playerX > e.x + 10.f) {
                profile.noteSaved(e.mode, e.x);
            }
            e.resolved = true;
        }
    }
}

void EbAssistOutcomes::runPostmortem(EbAssistController& controller, PlayLayer* playLayer,
                                   PlayerObject* player, GameObject* object, EbDecisionEntry& dec) {
    auto& profile = EbLevelProfile::get();
    auto& sim = EbShadowSimulator::get();

    int const age = controller.m_attemptTick - dec.tick;
    // destroyPlayer hot path — keep this to ONE short replay. Extra d±1 probes
    // (up to 3×96 physics ticks) were hitching deaths after assist.
    constexpr int kPostmortemMaxTicks = 32;
    if (age > 400) {
        profile.noteDiedAfterAssist(dec.mode, dec.x);
        dec.resolved = true;
        return;
    }
    int const maxTicks = std::min(kPostmortemMaxTicks, std::max(8, age + 2));

    auto report = sim.replay(playLayer, dec.snapshotSlot, dec.tick, dec.timeline,
                             controller.m_simDt, maxTicks, false, 0);

    bool const tickMatch = report.died && std::abs(report.deathTick - age) <= 1;
    bool const objMatch = !object || !report.deathObject || object == report.deathObject;
    bool const reproduced = tickMatch && objMatch;

    if (!reproduced) {
        profile.noteTrust(dec.mode, dec.x, 0.6f, "sim diverged");
    }
    profile.noteDiedAfterAssist(dec.mode, dec.x);
    if (!dec.assisted) {
        profile.noteEarliness(dec.mode, dec.x, 0, "postmortem pass");
    }

    dec.resolved = true;
}

void EbAssistOutcomes::beginOutcome(EbAssistController& controller, EbSimPlan const& plan,
                                  int effectTick) {
    controller.m_outcomePending = true;
    controller.m_outcomePlanId = plan.planId;
    controller.m_outcomeUntil = effectTick + 96;
    controller.m_outcomeExpectOrb = plan.requireOrb || plan.activatedRing;
    controller.m_outcomePlanDelay = plan.action == PlanAction::Delay || plan.delay > 0;
    controller.m_outcomeStartX = controller.m_lastPlayerX;
}

void EbAssistOutcomes::pollOutcome(EbAssistController& controller, PlayLayer* playLayer) {
    if (!controller.m_outcomePending || !playLayer)
        return;
    auto* player = controller.m_outcomePlayer1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player) {
        controller.m_outcomePending = false;
        return;
    }
    if (player->m_isDead) {
        EbAssistLog::debug(fmt::format("OUTCOME id={} death x={:.1f} orbExpect={} delay={}",
                                     controller.m_outcomePlanId, player->getPositionX(),
                                     controller.m_outcomeExpectOrb, controller.m_outcomePlanDelay));
        EbDivergenceTracker::get().note(controller.m_outcomePlanDelay, true,
                                      controller.m_outcomeExpectOrb, false);
        controller.m_outcomePending = false;
        return;
    }
    if (controller.m_attemptTick >= controller.m_outcomeUntil) {
        bool const orbHit = player->m_hasEverHitRing || player->m_touchedRing;
        EbAssistLog::debug(fmt::format("OUTCOME id={} alive dx={:.1f} orb={} expect={} delay={}",
                                     controller.m_outcomePlanId,
                                     player->getPositionX() - controller.m_outcomeStartX, orbHit,
                                     controller.m_outcomeExpectOrb, controller.m_outcomePlanDelay));
        EbDivergenceTracker::get().note(controller.m_outcomePlanDelay, false,
                                      controller.m_outcomeExpectOrb, orbHit);
        controller.m_outcomePending = false;
    }
}

void EbAssistOutcomes::logPlan(EbAssistController& controller, EbSimPlan const& plan, char const* kind) {
    bool const orbIntent = plan.requireOrb || plan.activatedRing || plan.usedOrbWindow;
    EbAssistLog::debug(fmt::format(
        "PLAN id={} kind={} action={} verdict={} delay={} orbIntent={} foresight={} eng={} "
        "requireOrb={} hold={} dMin={} dMax={} budgetHit={}",
        plan.planId, kind, plan.action == PlanAction::Pass ? "Pass" : "Delay",
        EbAssistVerdict::name(plan.reason), plan.delay, orbIntent ? 1 : 0,
        plan.foresightOrbPad ? 1 : 0, plan.trajectoryEngine ? "Trajectory" : "ShadowPlayer",
        plan.requireOrb, plan.requireHold, plan.dMin, plan.dMax, plan.budgetHit));
}

void EbAssistOutcomes::noteVerdict(EbAssistController& controller, AssistReason reason,
                                 char const* where) {
    controller.m_lastVerdict = reason;
    EbAssistLog::debug(fmt::format("VERDICT {} where={} overlay={}", EbAssistVerdict::name(reason),
                                 where ? where : "?", EbAssistVerdict::overlayBit(reason)));
}

void EbAssistOutcomes::onRealDeath(EbAssistController& controller, PlayLayer* playLayer,
                                 PlayerObject* player, GameObject* object) {
    if (!playLayer || !player)
        return;
    if (EbShadowSimulator::get().isSimulating())
        return;
    if (EbShadowSimulator::get().isOwnedGhost(player))
        return;
    (void)object;

    EbLagProbe::Scope const deathScope("eb.onRealDeath");
    EbHybridVerifyCache::get().clearAttempt();

    // destroyPlayer hot path — no physics replays, no disk, no bake merge.
    // GD physics is main-thread only; "multithread" death work is not viable.
    // Heavy learning/bake runs on resetAttempt (next attempt), not here.
    // Drop in-flight trigger-world slices (no flush); partial store may remain.
    EbTriggerWorldBaker::get().cancel();
    if (controller.m_dafWatch) {
        EbAssistRealMetrics::get().noteDeathAfterFire();
        controller.m_dafWatch = false;
    }
    if (controller.m_outcomePending) {
        EbLagProbe::Scope const state("eb.outcomeNote");
        EbDivergenceTracker::get().note(controller.m_outcomePlanDelay, true,
                                      controller.m_outcomeExpectOrb, false);
        controller.m_outcomePending = false;
    }

    // Seal Dev attempt on death (outcomes + flush). Next attempt opens on reset.
    {
        EbLagProbe::Scope const state("eb.devSealDeath");
        EbDevCycleRecorder::get().sealAttemptWithOutcomes("death", false, controller.m_attemptTick);
    }
    if (EbDevConfig::evalOnDeath() && EbDevCycleRecorder::get().isRecording()) {
        std::string evalErr;
        if (EbDevReport::evalLastAttempt(evalErr)) {
            controller.updateOverlay();
        } else if (!evalErr.empty()) {
            EbAssistLog::debug(fmt::format("Dev Lab eval-on-death: {}", evalErr));
        }
    }

    if (EbStageConfig::autoTune() && EbLevelProfile::get().loaded()) {
        if (auto* dec = controller.m_log.findForDeath(controller.m_attemptTick)) {
            EbLagProbe::Scope const state(
                "eb.learnDeath",
                fmt::format("assisted={} delay={} x={:.1f}", dec->assisted, dec->delay, dec->x));
            // Bookkeeping only — no ShadowSimulator::replay (was hitching deaths).
            EbLevelProfile::get().noteDiedAfterAssist(dec->mode, dec->x);
            if (!dec->assisted) {
                EbLevelProfile::get().noteEarliness(dec->mode, dec->x, 0, "death pass");
            }
            dec->resolved = true;
        }
        {
            EbLagProbe::Scope const state("eb.maybeAutosave");
            EbLevelProfile::get().maybeAutosave();
        }
    }

    controller.clearBuffer();
    controller.m_pendingRelease = false;
    controller.m_pendingReleaseHold = false;
    controller.m_pendingReleaseContinuous = false;
    controller.m_pendingReleaseArmTick = 0;
    controller.m_pendingReleaseUserUpTick = -1;
    controller.m_pendingReleaseMaxExtend = 0;
    controller.m_pendingReleaseTick = 0;
    {
        EbLagProbe::Scope const state("eb.saveCounterClear");
        EbSaveFrameCounter::get().clear();
    }
    {
        EbLagProbe::Scope const state("eb.syncButtons");
        controller.syncButtonState(playLayer);
    }
}
