#include "assist_input_runtime.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_clock_contract.hpp"
#include "../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../dev/capture/dev_trace_store.hpp"
#include "../../../ui/feedback/lag_warning_toast.hpp"
#include "../../../ui/feedback/save_feedback.hpp"
#include "../../../ui/visualizers/bake_path_visualizer.hpp"
#include "../../../ui/visualizers/trajectory_visualizer.hpp"
#include "../../bake/self_bake_recorder.hpp"
#include "../../diagnostics/ghost_parity_probe.hpp"
#include "../../sim/shadow_simulator.hpp"
#include "../../sim/sim_trust.hpp"
#include "../../trigger_world/trigger_world_baker.hpp"
#include "../assist_clock.hpp"
#include "../assist_controller.hpp"
#include "../assist_scope_gate.hpp"
#include "../contracts/assist_verdict.hpp"
#include "../input/click_intent_guard.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

void EbAssistInputRuntime::onPhysicsTick(EbAssistController& controller, PlayLayer* playLayer,
                                       float deltaTime) {
    EbPerfTrace::get().beginTick(controller.m_attemptTick, deltaTime);
    // endTick runs on every exit path (including early returns).
    class EbTickEndGuard {
    public:
        ~EbTickEndGuard() {
            EbPerfTrace::get().endTick();
        }

    private:
    } tickEndGuard;

    float timeWarp = 1.f;
    // Hook dt is GD step units (0.25 @ 240 TPS every tick — not a hitch).
    // Convert to seconds before sanity / EMA (v1.2.1 passed GD units through;
    // we keep seconds internally for TPS/bake and reconvert at the ghost).
    float const secDt = EbAssistClock::gdStepToSeconds(deltaTime);
    if (secDt > 0.f) {
        timeWarp = playLayer ? playLayer->m_gameState.m_timeWarp : 1.f;
        if (timeWarp <= 0.f)
            timeWarp = 1.f;
        float const simSample = secDt / timeWarp;
        // Real-seconds hitch/pause dumps must NOT stick in m_lastDt.
        auto& clock = EbAssistClock::get();
        if (clock.isSaneSimDt(simSample)) {
            if (controller.m_lastDt <= 0.f || controller.m_attemptTick < 3) {
                controller.m_lastDt = secDt;
            } else {
                controller.m_lastDt += 0.2f * (secDt - controller.m_lastDt);
            }
            controller.m_simDt = controller.m_lastDt / timeWarp;
        } else if (controller.m_simDt <= 0.f || !clock.isSaneSimDt(controller.m_simDt)) {
            controller.m_simDt = EbAssistClock::kNominalSimDt;
            controller.m_lastDt = controller.m_simDt * timeWarp;
        }
        float const beforeSnap = controller.m_simDt;
        controller.m_simDt = clock.planSimDtOrNominal(controller.m_simDt, timeWarp);
        if (std::fabs(controller.m_simDt - beforeSnap) > 1e-7f) {
            // Keep lastDt coherent with snapped planning quantum.
            controller.m_lastDt = controller.m_simDt * timeWarp;
        }

        float const errorMessage = secDt - controller.m_dtEma;
        controller.m_dtEma += 0.1f * errorMessage;
        float const denom = std::max(controller.m_dtEma, 1e-4f);
        controller.m_dtVarEma =
            0.1f * std::abs(errorMessage) / denom + 0.9f * controller.m_dtVarEma;
    }
    ++controller.m_attemptTick;
    {
        double const simTps = EbAssistClock::get().simTpsFromDt(controller.m_simDt);
        if (EbAssistClock::get().noteSpeedScale(timeWarp, simTps, controller.m_attemptTick)) {
            EbAssistLog::debug(fmt::format("speed-scale: suspect simTps={:.1f} wallTps={:.1f} "
                                         "tw={:.3f} simDt={:.6f} lastDt={:.6f}",
                                         simTps,
                                         EbAssistClock::get().wallTpsFromDt(controller.m_lastDt),
                                         timeWarp, controller.m_simDt, controller.m_lastDt));
            EbLagProbe::get().mark("eb.speed.scale",
                                 fmt::format("tps={:.1f} tw={:.3f}", simTps, timeWarp));
        }
    }
    if (!playLayer)
        return;

    {
        EbPerfTrace::Scope const scope("pollOutcome");
        controller.pollOutcome(playLayer);
    }
    {
        EbPerfTrace::Scope const scope("flushPendingRelease");
        controller.flushPendingRelease(playLayer);
    }
    {
        EbPerfTrace::Scope const scope("saveFeedbackTick");
        EbSaveFeedback::get().onPhysicsTick();
    }
    {
        EbPerfTrace::Scope const scope("lagWarningToast");
        // Same seconds clock as sim EMA — raw GD step units (~0.25) would
        // burn the 5s hold in ~20 physics ticks (~instant despawn).
        EbLagWarningToast::get().onPhysicsTick(secDt > 0.f ? secDt : EbAssistClock::kNominalSimDt);
    }
    {
        EbPerfTrace::Scope const scope("selfBakeIdle");
        EbSelfBakeRecorder::get().onPhysicsTick(playLayer);
    }
    {
        EbPerfTrace::Scope const scope("triggerBakeSlice");
        EbTriggerWorldBaker::get().kickIfStaleEmpty(playLayer);
        EbTriggerWorldBaker::get().tickSlice();
    }

    auto* playerOne = playLayer->m_player1;
    if (playerOne) {
        {
            EbPerfTrace::Scope const scope("ghostParity");
            EbGhostParityProbe::get().onPhysicsTick(playLayer, playerOne, controller.m_simDt,
                                                  controller.m_attemptTick);
        }
        // Drift mid-buffer → drop Delay; never fire a plan made under bad trust.
        if (EbSimTrust::get().parityDrift() && controller.m_buffer.state == BufferState::Buffered) {
            EbAssistLog::debug("parity drift → clear buffer (force Pass)");
            controller.clearBuffer();
            controller.syncButtonState(playLayer);
            controller.noteVerdict(AssistReason::PassParityDrift, "parity-clear-buffer");
        }
        controller.m_lastPlayerX = playerOne->getPositionX();
        controller.m_lastMode = EbStageConfig::kindOf(playerOne);
        if (!controller.m_startSnapshotTaken && controller.m_attemptTick <= 2) {
            EbPerfTrace::Scope const scope("captureStartSnapshot");
            controller.m_startSnapshotSlot = EbShadowSimulator::get().captureStartSnapshot(playerOne);
            controller.m_startSnapshotTaken = controller.m_startSnapshotSlot >= 0;
        }

        bool const modeChanged =
            controller.m_modeKnown && (controller.m_modeShip != playerOne->m_isShip ||
                                       controller.m_modeBall != playerOne->m_isBall ||
                                       controller.m_modeBird != playerOne->m_isBird ||
                                       controller.m_modeDart != playerOne->m_isDart ||
                                       controller.m_modeRobot != playerOne->m_isRobot ||
                                       controller.m_modeSpider != playerOne->m_isSpider ||
                                       controller.m_modeSwing != playerOne->m_isSwing);
        controller.m_modeShip = playerOne->m_isShip;
        controller.m_modeBall = playerOne->m_isBall;
        controller.m_modeBird = playerOne->m_isBird;
        controller.m_modeDart = playerOne->m_isDart;
        controller.m_modeRobot = playerOne->m_isRobot;
        controller.m_modeSpider = playerOne->m_isSpider;
        controller.m_modeSwing = playerOne->m_isSwing;
        controller.m_modeKnown = true;

        {
            EbPerfTrace::Scope const scope("devModeTimeline");
            EbDevCycleRecorder::get().pollModeTimeline(playLayer, controller.m_attemptTick);
        }

        if (modeChanged && controller.m_buffer.state != BufferState::Idle) {
            EbAssistLog::debug("mode change → delivering buffered input");
            EbPerfTrace::Scope const scope("fireBuffered.modeChange");
            controller.fireBuffered(playLayer, playerOne, false);
            controller.updateOverlay();
        }

        {
            EbPerfTrace::Scope const scope("tickLearning");
            controller.tickLearning(playLayer, playerOne);
        }
    }

    {
        EbPerfTrace::Scope const scope("trajectory");
        EbTrajectoryVisualizer::get().onPhysicsTick(playLayer, deltaTime);
    }
    {
        EbPerfTrace::Scope const scope("bakePath");
        EbBakePathVisualizer::get().onPhysicsTick(playLayer, deltaTime);
    }

    controller.pruneClickTimes();
    int const cpsLim = EbStageConfig::cpsLimit();
    if (cpsLim > 0) {
        bool const limited = controller.currentCps() >= cpsLim;
        if (controller.m_cpsLocked && !limited) {
            controller.m_cpsLocked = false;
            EbAssistLog::info(
                fmt::format("CPS: unlocked (now {}/{})", controller.currentCps(), cpsLim));
        }
        if (limited && controller.m_buffer.state == BufferState::Buffered && playerOne) {
            EbPerfTrace::Scope const scope("fireBuffered.cps");
            controller.fireBuffered(playLayer, playerOne, false);
        }
    }

    if (controller.m_buffer.state == BufferState::Idle) {
        controller.updateOverlay();
        return;
    }

    auto* player = controller.m_buffer.player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player || player->m_isDead) {
        controller.clearBuffer();
        controller.syncButtonState(playLayer);
        controller.updateOverlay();
        return;
    }

    if (controller.m_buffer.state == BufferState::Buffered) {
        if (controller.m_buffer.holdUntilSafe) {
            EbPerfTrace::Scope const scope("tickHoldUntilSafe");
            controller.tickHoldUntilSafe(playLayer, player);
        }
        if (controller.m_buffer.state == BufferState::Buffered) {
            bool const due = controller.shouldFireBuffered(
                controller.m_attemptTick, controller.m_buffer.fireTick,
                controller.m_buffer.pressTick, controller.m_buffer.planDelay,
                controller.m_buffer.limit);
            if (due && (controller.m_buffer.firePhase <= 0.f ||
                        controller.m_attemptTick > controller.m_buffer.fireTick)) {
                EbPerfTrace::Scope const scope("fireBuffered");
                controller.fireBuffered(playLayer, player);
            }
        }
    } else if (controller.m_buffer.state != BufferState::Idle) {
        controller.clearBuffer();
    }
    controller.updateOverlay();
}

bool EbAssistInputRuntime::onHandleButton(EbAssistController& controller, GJBaseGameLayer* layer,
                                        bool down, int button, bool player1) {
    if (controller.m_injecting)
        return true;
    if (button != EbAssistControllerImplementation::JUMP_BUTTON)
        return true;

    auto* playLayer = geode::prelude::typeinfo_cast<PlayLayer*>(layer);
    if (!playLayer)
        return true;

    auto* player = player1 ? playLayer->m_player1 : playLayer->m_player2;
    if (!player)
        return true;

    controller.m_userHolding[player1 ? 0 : 1] = down;

    // Second click while a hold-assist release is armed: cancel it and re-plan.
    if (down && controller.m_pendingRelease && controller.m_pendingReleaseP1 == player1) {
        controller.m_pendingRelease = false;
        controller.m_pendingReleaseHold = false;
        controller.m_pendingReleaseContinuous = false;
        controller.m_pendingReleaseUserUpTick = -1;
        controller.m_pendingReleaseMaxExtend = 0;
        controller.m_pendingReleaseDevPressId = 0;
        EbAssistLog::debug("hold-assist: cancel pending release (re-click)");
    }

    // Attribution: see resolveEffectTick / assist_reference.md "effectTick".
    // m_inRealUpdate ↔ inside PlayerObject::update (CBF mid-step).
    bool const midStep = controller.m_inRealUpdate;
    bool const continuous = controller.continuousModePlayer(player);
    int const effectTick =
        controller.resolveEffectTick(controller.m_attemptTick, midStep, continuous);

    auto finish = [&](bool passThrough) -> bool {
        if (passThrough) {
            EbSelfBakeRecorder::get().noteVanillaInput(playLayer, player, player1, down, effectTick,
                                                     controller.m_simDt);
        }
        return passThrough;
    };

    int const playerIndex = player1 ? 0 : 1;
    auto beginDevPress = [&]() {
        EbDevCycleRecorder::get().beginPress(playerIndex, down, effectTick, controller.m_attemptTick,
                                           DevEventSource::Live);
    };

    // Min click interval: skip assist only — never block the vanilla click.
    if (down) {
        int const minMs = EbStageConfig::minClickIntervalMs();
        if (minMs > 0 && !controller.m_clickTimes.empty()) {
            auto const elapsed = std::chrono::steady_clock::now() - controller.m_clickTimes.back();
            if (elapsed < std::chrono::milliseconds(minMs)) {
                EbAssistLog::debug(fmt::format(
                    "click debounce: no assist ({}ms < {}ms), pass through",
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), minMs));
                beginDevPress();
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassDebounce, "debounce");
                controller.noteVerdict(AssistReason::PassDebounce, "debounce");
                return finish(true);
            }
        }
        controller.noteUserClick();
        EbClickIntentGuard::get().notePress(playerIndex, effectTick, EbStageConfig::kindOf(player));
    }

    // firstDt: full sim dt between frames; CBF remainder / timeWarp when mid-step.
    // Never fold the remainder into effectTick (that caused ±1 vs between-frames).
    float timeWarp = playLayer->m_gameState.m_timeWarp;
    if (timeWarp <= 0.f)
        timeWarp = 1.f;
    float firstDt = controller.m_simDt;
    if (midStep) {
        float rem = controller.m_lastDt - controller.m_subDtSeen;
        float const minDt = controller.m_lastDt * 0.02f;
        if (rem < minDt)
            rem = minDt;
        if (rem > controller.m_lastDt)
            rem = controller.m_lastDt;
        firstDt = rem / timeWarp;
        if (firstDt > controller.m_simDt)
            firstDt = controller.m_simDt;
    }
    EbDevClockContract::Stamp const clockStamp = EbDevClockContract::get().makeStamp(
        controller.m_attemptTick, midStep, continuous, controller.m_simDt, firstDt,
        controller.m_lastDt, timeWarp, DevSimPathId::None);

    // Attribution log only under debug-logging (not every press into perf file).
    if (down && EbStageConfig::debugLogging()) {
        EbAssistLog::debug(fmt::format("attrib: effectTick={} attemptTick={} midStep={} "
                                     "continuous={} firstDt={:.6f} simDt={:.6f} p{}",
                                     effectTick, controller.m_attemptTick, midStep ? 1 : 0,
                                     continuous ? 1 : 0, firstDt, controller.m_simDt,
                                     player1 ? 1 : 2));
    }

    // Track real (non-swallowed) inputs for learning timelines.
    // Buffered/swallowed ones are recorded when we inject or as releaseSeen.
    bool const ours =
        controller.m_buffer.state != BufferState::Idle && controller.m_buffer.player1 == player1;

    if (!down) {
        if (ours) {
            if (!controller.m_buffer.down) {
                return false;
            }
            controller.m_buffer.releaseSeen = true;
            controller.m_buffer.releaseTick = effectTick;
            controller.m_log.noteInput(effectTick, false);
            EbDevCycleRecorder::get().noteReleaseSeen(controller.m_buffer.devPressId,
                                                      controller.m_attemptTick);
            // Early UP does not cancel Delay: stay Buffered, fire still due.
            EbAssistLog::debug(fmt::format(
                "buffer: early UP → releaseSeen=1 stay Buffered fireTick={} attempt={} p{}",
                controller.m_buffer.fireTick, controller.m_attemptTick, player1 ? 1 : 2));
            return false;
        }

        // Extend-hold: user let go — swallow so Jump can stay down ≤ maxExtend ticks.
        if (controller.m_pendingRelease && controller.m_pendingReleaseP1 == player1) {
            if (controller.m_pendingReleaseUserUpTick < 0) {
                controller.m_pendingReleaseUserUpTick = controller.m_attemptTick;
                controller.m_pendingReleaseTick =
                    controller.m_pendingReleaseUserUpTick + controller.m_pendingReleaseMaxExtend;
            }
            if (controller.m_pendingReleaseDevPressId > 0) {
                EbDevCycleRecorder::get().noteHoldSwallow(controller.m_pendingReleaseDevPressId,
                                                          controller.m_attemptTick);
            }
            EbAssistLog::debug(
                fmt::format("hold-assist: swallow user release @{} extend≤{} deadline={}",
                            controller.m_attemptTick, controller.m_pendingReleaseMaxExtend,
                            controller.m_pendingReleaseTick));
            return false;
        }

        if (!controller.shouldAssistPlayer(player) || !EbStageConfig::assistRelease()) {
            beginDevPress();
            if (controller.cpsLimited()) {
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassCps, "release-cps");
                controller.noteVerdict(AssistReason::PassCps, "release-cps");
            } else if (EbAssistScopeGate::blockReason(playLayer, player)) {
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassScope, "release-scope");
                controller.noteVerdict(AssistReason::PassScope, "release-scope");
            } else {
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassAssistOff, "release-off");
                controller.noteVerdict(AssistReason::PassAssistOff, "release-off");
            }
            return finish(true);
        }
        if (controller.m_attemptTick <= 2) {
            beginDevPress();
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassEarlyAttempt, "release-early");
            controller.noteVerdict(AssistReason::PassEarlyAttempt, "release-early");
            return finish(true);
        }
        beginDevPress();
        bool const pass =
            controller.planRelease(playLayer, player, player1, effectTick, firstDt, clockStamp);
        if (pass)
            controller.m_log.noteInput(effectTick, false);
        return finish(pass);
    }

    if (ours && controller.m_buffer.state == BufferState::Buffered && controller.m_buffer.down) {
        // Second DOWN while A is still armed:
        //  - after early UP (releaseSeen): classic re-click → fire A, plan B
        //  - still holding (multi-key double-click): never swallow — GD must see
        //    both clicks. Deliver A now, then fall through to plan B.
        EbAssistLog::debug(
            fmt::format("buffer: 2nd DOWN while armed (releaseSeen={}) → fire A then plan B",
                        controller.m_buffer.releaseSeen ? 1 : 0));
        controller.fireBuffered(playLayer, player, false);
    }
    if (ours && controller.m_buffer.state == BufferState::Buffered && !controller.m_buffer.down) {
        controller.fireBuffered(playLayer, player, false);
    }

    if (!controller.shouldAssistPlayer(player) || !EbStageConfig::assistPress()) {
        beginDevPress();
        if (controller.cpsLimited()) {
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassCps, "press-cps");
            controller.noteVerdict(AssistReason::PassCps, "press-cps");
        } else if (EbAssistScopeGate::blockReason(playLayer, player)) {
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassScope, "press-scope");
            controller.noteVerdict(AssistReason::PassScope, "press-scope");
        } else {
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassAssistOff, "press-off");
            controller.noteVerdict(AssistReason::PassAssistOff, "press-off");
        }
        return finish(true);
    }
    if (controller.m_attemptTick <= 2) {
        beginDevPress();
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassEarlyAttempt, "press-early");
        controller.noteVerdict(AssistReason::PassEarlyAttempt, "press-early");
        return finish(true);
    }
    beginDevPress();
    bool const pass =
        controller.planPress(playLayer, player, player1, effectTick, firstDt, clockStamp);
    if (pass)
        controller.m_log.noteInput(effectTick, true);
    return finish(pass);
}

bool EbAssistInputRuntime::allowPushButton(EbAssistController& controller, PlayerObject* player,
                                         PlayerButton button) {
    if (controller.m_injecting)
        return true;
    if (button != PlayerButton::Jump)
        return true;
    if (controller.m_buffer.state != BufferState::Buffered || !controller.m_buffer.down)
        return true;

    auto* playLayer = PlayLayer::get();
    if (!playLayer || !player)
        return true;

    bool const isP1 = player == playLayer->m_player1;
    if (!(player != playLayer->m_player1 && player != playLayer->m_player2)) {
        return controller.m_buffer.player1 != isP1;
    }
    return true;
}
