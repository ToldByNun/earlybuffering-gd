#include "shadow_plan_context.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../assist/contracts/selected_tick_guard.hpp"
#include "../../../assist/planning/assist_horizon.hpp"
#include "../../mode_survival.hpp"
#include "../../shadow_simulator.hpp"
#include "../shadow_survival_rules.hpp"
#include "../simulation_candidate.hpp"
#include "../simulation_plan.hpp"
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

EbShadowPlanContext::EbShadowPlanContext(EbShadowSimulator& simulator, PlayLayer* layer,
                                     PlayerObject* real, bool down, int limit, float deltaTime,
                                     float firstDt, EbPlanOptions const& options, EbSimPlan& plan)
    : simulator(simulator), layer(layer), real(real), down(down), limit(limit),
      deltaTime(deltaTime), firstDt(firstDt), options(options), plan(plan),
      model(EbShadowSimulator::modelFor(real, limit, options.capMul)),
      robustK(EbShadowSurvivalRules::resolveRobustK(real, options)),
      latencyWeight(std::max(0.f, options.latencyWeight)),
      tickBudget(options.tickBudget > 0 ? options.tickBudget : EbShadowSimulator::kDefaultTickBudget),
      useTrajEngine(!options.legacyPick &&
                    options.assistEngine == EbStageConfig::AssistEngineKind::Trajectory) {
    plan.trajectoryEngine = useTrajEngine;
    model.continuePastClear = options.requireAliveForDelay;
    // Align classic IW with Adaptive discrete clamp (Assist Pack AP7): without a
    // manual mode/look-ahead cap, cube/ball horizons stay inside the ~96 tick
    // band so observation floors stay reachable after prove ticks.
    {
        bool const continuousControl =
            real && (real->m_isDart || real->m_isShip || real->m_isSwing);
        bool const inputWindow = options.requireAliveForDelay && (down || continuousControl);
        if (inputWindow && real && EbStageConfig::paramsFor(real).cap == 0 &&
            EbStageConfig::lookAheadCap() == 0) {
            if (continuousControl) {
                model.cap = EbAssistHorizon::continuousInputWindowCap(
                    EbStageConfig::kindOf(real), limit, options.capMul);
            } else {
                model.cap = std::min(model.cap, std::max(1, 96 - limit));
            }
        }
    }
    // Live Delay proof: run a few ticks past the nominal horizon so a death on
    // the next frame after hitCap is not declared Safe (session Dump-SP class).
    if (options.requireAliveForDelay && model.cap > 0) {
        model.cap += EbShadowSurvivalRules::kPostCapProveTicks;
    }
    plan.continuous = model.continuous;
    robustK = EbShadowSurvivalRules::clampRobustKToWindow(robustK, limit);
    if (EbStageConfig::assistVerifyAb())
        tickBudget *= 2;
    // Assist Pack AP-L1/L5: mid-run soft-wall abort shares this press clock.
    // Adaptive→classic fallthrough reuses Adaptive's wall0 + full press budgetUs
    // so remUs is never reinvented past sim-budget-us.
    if (options.reusePlanWall) {
        wall0 = options.planWall0;
    } else {
        wall0 = std::chrono::steady_clock::now();
    }
    simulator.beginPlanWall(wall0, options.budgetUs);
}

EbDevRejectLedger::EarlyExit EbShadowPlanContext::ledgerEarlyExit() const {
    if (plan.budgetHit)
        return EbDevRejectLedger::EarlyExit::Budget;
    if (ledgerPreferEarlyExit)
        return EbDevRejectLedger::EarlyExit::PreferBand;
    if (ledgerSameCrashExit)
        return EbDevRejectLedger::EarlyExit::SameCrash;
    if (ledgerEarliestSafeExit)
        return EbDevRejectLedger::EarlyExit::EarliestSafe;
    if (ledgerPeakDeathExit)
        return EbDevRejectLedger::EarlyExit::PeakDeath;
    return EbDevRejectLedger::EarlyExit::None;
}

bool EbShadowPlanContext::wallExceeded() const {
    // Assist Pack AP-L1: shared mid-run latch + soft-wall clock from beginPlanWall.
    return simulator.planWallExceeded();
}

int EbShadowPlanContext::wallRemUs() const {
    if (options.budgetUs <= 0)
        return -1;
    if (simulator.planWallHit())
        return 0;
    auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - wall0)
                                         .count();
    int const rem = options.budgetUs - static_cast<int>(elapsedMicroseconds);
    return rem > 0 ? rem : 0;
}

bool EbShadowPlanContext::linkedDual() const {
    if (!layer || !layer->m_player2 || !real)
        return false;
    if (!layer->m_gameState.m_isDualMode)
        return false;
    // "0010" Flip2PlayerControls → split buffers; linked = shared jump for both.
    if (auto* gm = GameManager::sharedState()) {
        if (gm->getGameVariable("0010"))
            return false;
    }
    return real == layer->m_player1 || real == layer->m_player2;
}

EbShadowSimulator::RunResult EbShadowPlanContext::runEngine(PlayerObject* who, int delay,
                                                        int releaseAfter, EbThreatZone const* z) {
    if (useTrajEngine) {
        return simulator.runTrajectoryOne(layer, who, down, delay, limit, deltaTime, firstDt, model,
                                          releaseAfter, z, &tickBudget);
    }
    return simulator.runOne(layer, who, down, delay, limit, deltaTime, firstDt, model, releaseAfter,
                            z, &tickBudget);
}

void EbShadowPlanContext::publishLedger(int chosenDelay, bool doPassFlag,
                                      EbDevRejectLedger::EarlyExit early) {
    guardSelectedTick();
    if (plan.guardOriginalDelay > 0) {
        chosenDelay = plan.delay;
        doPassFlag = plan.action == PlanAction::Pass;
    }
    EbDevRejectLedger::BuildInput in;
    in.limit = limit;
    in.chosenDelay = chosenDelay;
    in.doPass = doPassFlag;
    in.latencyWeight = latencyWeight;
    in.orbIntent = ledgerOrbIntent;
    in.actMin = ledgerActMin;
    in.actMax = ledgerActMax;
    in.legacyPick = options.legacyPick;
    in.earlyExit = early;
    in.includeScores = EbDevConfig::recordCands();
    in.snaps.assign(static_cast<size_t>(limit) + 1, {});
    for (int delay = 0; delay <= limit; ++delay) {
        if (!candReady[static_cast<size_t>(delay)])
            continue;
        auto const& candidate = cands[static_cast<size_t>(delay)];
        auto& s = in.snaps[static_cast<size_t>(delay)];
        s.evaluated = true;
        s.viable = candidate.viable;
        s.diedBeforeAction = candidate.diedBeforeAction;
        s.dualFailed = candidate.dualFailed;
        s.unknownTrunc = candidate.unknownTrunc;
        s.clearsThreat = candidate.clearsThreat;
        s.activatedRing = candidate.activatedRing;
        s.robustKFailed = candidate.robustKFailed;
        s.verifyFailed = candidate.verifyFailed;
        s.surviveBin = candidate.surviveBin;
        s.endX = candidate.endX;
        // AP4: structured candidate truth for offline multi-mode research.
        s.hasRunTruth = true;
        s.deathTick = candidate.raw.deathTick;
        s.ticksRan = candidate.raw.ticksRan;
        s.unknown = candidate.raw.unknown || candidate.raw.unknownTrunc;
        s.surviveTicks = candidate.surviveTicks;
        // Assist Pack AP1: depth-truncated Alive (short ticksRan, not dead) is a
        // budget/depth issue — never classify as opaque not-viable in the ledger.
        bool const aliveShort = !candidate.raw.died && !candidate.raw.unknown &&
                                !candidate.raw.unknownTrunc && candidate.raw.ticksRan > 0 &&
                                candidate.raw.ticksRan < model.cap;
        s.budgetHit =
            (plan.budgetHit && aliveShort) || (aliveShort && !candidate.viable);
        auto const cls = EbModeSurvival::classify(
            EbShadowSurvivalRules::survivalInputFrom(candidate.raw, zone, model, real));
        s.survivalClass = EbModeSurvival::classId(cls);
    }
    auto ledger = EbDevRejectLedger::build(in);
    for (auto& note : ledger.notes) {
        if (note.delay < 0 || note.delay > limit || !candReady[note.delay])
            continue;
        auto const& run = cands[note.delay].raw;
        if (cands[note.delay].recoveryPressTick >= 0) {
            note.detail += fmt::format(" conditional-next-player-press={} recovery-verified=true",
                                       cands[note.delay].recoveryPressTick);
            note.survivalClass = "recoverable";
            if (run.orbContinuationTick >= 0)
                note.detail += " conditional-orb-chain=true";
            else if (real->m_isSpider)
                note.detail += " conditional-spider-chain=true";
        }

        note.detail += fmt::format(" ticks={} died={} deathTick={} unknown={} ballLanding={}",
                                   run.ticksRan, run.died, run.deathTick,
                                   run.unknown || run.unknownTrunc, run.landedAfterAction);
    }
    plan.candNotes = std::move(ledger.notes);
    plan.candNotesTruncated = ledger.truncated;
}

EbSimulationCandidate EbShadowPlanContext::evalDelay(int delayValue, int releaseAfter,
                                                     bool probeOrbContinuation) {
    EbSimulationCandidate candidate;
    candidate.delay = delayValue;
    if (tickBudget <= 0 || wallExceeded()) {
        plan.budgetHit = true;
        return candidate;
    }
    EbShadowSimulator::RunResult result;
    if (probeOrbContinuation && real->m_isBird && down && !useTrajEngine && !linkedDual()) {
        auto continuationModel = model;
        continuationModel.probeOrbContinuation = true;
        result = simulator.runOne(layer, real, down, delayValue, limit, deltaTime, firstDt,
                                  continuationModel, releaseAfter, zone.valid ? &zone : nullptr,
                                  &tickBudget);
    } else {
        result = runEngine(real, delayValue, releaseAfter, zone.valid ? &zone : nullptr);
    }
    simulator.resetCaptureForNextRun(layer);
    ++plan.candidatesTried;
    // Assist Pack AP-L1: mid-run soft-wall abort must surface as budgetHit.
    if (simulator.planWallHit())
        plan.budgetHit = true;
    candidate.raw = result;
    candidate.recoveryPressTick = result.orbContinuationTick;
    candidate.diedBeforeAction = result.diedBeforeAction;
    candidate.activatedRing = result.activatedRing;
    candidate.unknownTrunc = result.unknownTrunc;
    candidate.toggleUnknown = result.toggleUnknown;
    if (candidate.toggleUnknown || (candidate.unknownTrunc && hadToggleTargets)) {
        sawToggleUnknown = true;
    }
    candidate.endX = result.endX;
    candidate.deathX = result.deathX;
    candidate.jumpTick = result.jumped ? result.jumpTick : delayValue;
    candidate.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(result);
    candidate.surviveBin = EbShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
    candidate.requireHold = false; // set only if tap fails and hold-branch wins
    return candidate;
}

void EbShadowPlanContext::finalizeViable(EbSimulationCandidate& candidate, int releaseAfter) {
    candidate.clearsThreat = EbShadowSurvivalRules::clearsThreatOf(candidate.raw, zone);
    // clearsThreat + ModeSurvival (soft bake vs Live Delay-worthy Alive).
    candidate.viable =
        EbShadowSurvivalRules::runResultDelaySafe(candidate.raw, zone, model, real, requireAlive);
    // Trust-long — alive path must run far enough to see continue
    // (platform after jump). Budget-truncated short runs are not failed here.
    if (candidate.viable && minSurviveLong > 0) {
        bool const truncated = !candidate.raw.died && !candidate.raw.unknownTrunc &&
                               candidate.raw.ticksRan < model.cap &&
                               candidate.raw.ticksRan < minSurviveLong;
        if (!truncated) {
            auto const cls = EbModeSurvival::classify(
                EbShadowSurvivalRules::survivalInputFrom(candidate.raw, zone, model, real));
            bool const longSafe = requireAlive ? EbModeSurvival::countsAsDelayWorthy(cls)
                                               : EbModeSurvival::countsAsSafeForDelay(cls);
            if (!EbAssistHorizon::longPathSuccessOk(candidate.surviveTicks, candidate.raw.died,
                                                  longSafe, minSurviveLong)) {
                candidate.viable = false;
            }
        }
    }
    // Linked dual: other player's shadow must also survive the same plan.
    if (candidate.viable && linkedDual()) {
        PlayerObject* other = (real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
        if (other && tickBudget > 0) {
            auto r2 = runEngine(other, candidate.delay, releaseAfter, zone.valid ? &zone : nullptr);
            simulator.resetCaptureForNextRun(layer);
            if (!EbShadowSurvivalRules::runResultDelaySafe(r2, zone, model, other, requireAlive)) {
                candidate.viable = false;
                candidate.clearsThreat = false;
                candidate.dualFailed = true;
            }
        }
    }
}

bool EbShadowPlanContext::jitterOk(int delay, int releaseAfter, bool force) {
    if ((!options.robustJitter && !force) || delay < 0)
        return true;
    if (delay <= limit && cands[static_cast<size_t>(delay)].jitterVerified &&
        cands[static_cast<size_t>(delay)].jitterVerifiedReleaseAfter == releaseAfter)
        return true;
    if (tickBudget <= 0 || wallExceeded()) {
        plan.guardProbeIncomplete = true;
        return true;
    }
    float const variation = std::clamp(options.dtVariance, 0.03f, 0.1f);
    float const kScales[] = {1.f - variation, 1.f + variation};
    for (float state : kScales) {
        if (tickBudget <= 0 || wallExceeded()) {
            plan.guardProbeIncomplete = true;
            return true;
        }
        ++plan.guardProbeCount;
        float jd = firstDt * state;
        if (jd <= 0.f || jd > deltaTime * 1.5f)
            jd = firstDt;
        EbShadowSimulator::RunResult result;
        if (useTrajEngine) {
            result =
                simulator.runTrajectoryOne(layer, real, down, delay, limit, deltaTime, jd, model,
                                           releaseAfter, zone.valid ? &zone : nullptr, &tickBudget);
        } else {
            result = simulator.runOne(layer, real, down, delay, limit, deltaTime, jd, model,
                                      releaseAfter, zone.valid ? &zone : nullptr, &tickBudget);
        }
        simulator.resetCaptureForNextRun(layer);
        plan.budgetHit = plan.budgetHit || simulator.planWallHit();
        auto const probeState = EbSelectedTickGuard::classifyProbe(
            result.died || result.diedBeforeAction,
            result.unknown || result.unknownTrunc || result.toggleUnknown, result.landedAfterAction,
            result.ticksRan, model.cap, EbShadowSurvivalRules::clearsThreatOf(result, zone));
        if (probeState == EbSelectedTickGuard::ProbeState::Rejected)
            return false;
        if (probeState == EbSelectedTickGuard::ProbeState::Incomplete) {
            plan.guardProbeIncomplete = true;
            return true;
        }
    }
    if (delay <= limit) {
        cands[static_cast<size_t>(delay)].jitterVerified = true;
        cands[static_cast<size_t>(delay)].jitterVerifiedReleaseAfter = releaseAfter;
    }
    return true;
}

void EbShadowPlanContext::guardSelectedTick() {
    if (!options.requireAliveForDelay || plan.action != PlanAction::Delay || plan.delay <= 0 ||
        plan.delay > limit || plan.guardOriginalDelay >= 0 || linkedDual())
        return;
    plan.guardOriginalDelay = plan.delay;
    auto const compatible = [&](int delay) {
        auto const& candidate = cands[static_cast<size_t>(delay)];
        auto const& original = cands[static_cast<size_t>(plan.guardOriginalDelay)];
        bool const known = !candidate.raw.unknown && !candidate.raw.unknownTrunc &&
                           !candidate.raw.toggleUnknown && !candidate.unknownTrunc;
        bool const complete = candidate.recoveryPressTick >= 0 ||
                              (!candidate.raw.died && (candidate.raw.landedAfterAction ||
                                                       candidate.raw.ticksRan >= model.cap));
        return candReady[static_cast<size_t>(delay)] && candidate.viable && known && complete &&
               (!plan.requireOrb ||
                (candidate.activatedRing && (plan.dMin < 0 || delay >= plan.dMin) &&
                 (plan.dMax < 0 || delay <= plan.dMax))) &&
               candidate.raw.wasDashing == original.raw.wasDashing &&
               candidate.requireHold == original.requireHold &&
               candidate.releaseAfter == original.releaseAfter;
    };
    std::vector<int> verified;
    for (int delay = 1; delay <= limit; ++delay) {
        if (compatible(delay))
            verified.push_back(delay);
    }
    plan.viableDelays = verified;
    // An earlier gate may have a different proof contract. Do not reinterpret
    // that candidate as fully checked here.
    if (std::ranges::find(verified, plan.delay) == verified.end()) {
        plan.guardProbeIncomplete = true;
        return;
    }
    int selected = EbSelectedTickGuard::choose(plan.delay, limit, robustK, verified);
    for (int attempt = 0; attempt < 2 && selected > 0; ++attempt) {
        bool const interior = EbSelectedTickGuard::hasMargin(selected, verified);
        if (interior && !options.robustJitter)
            break;
        // With jitter disabled, require a measured timing disturbance before
        // perturbing an already-proven input.
        if (!options.robustJitter && options.dtVariance < 0.03f) {
            plan.guardProbeIncomplete = true;
            break;
        }
        auto const& candidate = cands[static_cast<size_t>(selected)];
        int const followup = candidate.recoveryPressTick >= 0
                                 ? candidate.recoveryPressTick - selected
                                 : candidate.releaseAfter;
        if (jitterOk(selected, followup, true))
            break;
        cands[static_cast<size_t>(selected)].verifyFailed = true;
        cands[static_cast<size_t>(selected)].viable = false;
        std::erase(verified, selected);
        selected = attempt == 1 ? -1
                                : EbSelectedTickGuard::choose(plan.guardOriginalDelay, limit,
                                                              robustK, verified);
    }
    plan.viableDelays = verified;
    if (selected <= 0) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassRobustFail;
        plan.dMin = plan.dMax = -1;
        plan.requireHold = false;
        plan.releaseAfter = plan.releaseEarliest = plan.releaseLatest = -1;
        plan.delay = plan.jumpTick = 0;
        plan.nowIsSafe = false;
        plan.threatCleared = false;
        return;
    }
    if (selected == plan.guardOriginalDelay)
        return;
    auto const& winner = cands[static_cast<size_t>(selected)];
    plan.delay = selected;
    plan.jumpTick = winner.jumpTick;
    plan.requireHold = plan.requireHold || winner.requireHold;
    if (winner.releaseAfter >= 0)
        plan.releaseAfter = winner.releaseAfter;
    plan.activatedRing = winner.activatedRing;
}
