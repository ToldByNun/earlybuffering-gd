#include "shadow_plan_context.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../../dev/evaluation/dev_reject_ledger.hpp"
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
    // Live Delay proof: run a few ticks past the nominal horizon so a death on
    // the next frame after hitCap is not declared Safe (session Dump-SP class).
    if (options.requireAliveForDelay && model.cap > 0) {
        model.cap += EbShadowSurvivalRules::kPostCapProveTicks;
    }
    plan.continuous = model.continuous;
    robustK = EbShadowSurvivalRules::clampRobustKToWindow(robustK, limit);
    if (EbStageConfig::assistVerifyAb())
        tickBudget *= 2;
    wall0 = std::chrono::steady_clock::now();
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
    if (options.budgetUs <= 0)
        return false;
    auto elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - wall0)
                                   .count();
    return elapsedMicroseconds >= options.budgetUs;
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
        s.budgetHit = plan.budgetHit && !candidate.raw.died && !candidate.raw.unknown &&
                      candidate.raw.ticksRan > 0 && candidate.raw.ticksRan < model.cap;
        auto const cls = EbModeSurvival::classify(
            EbShadowSurvivalRules::survivalInputFrom(candidate.raw, zone, model, real));
        s.survivalClass = EbModeSurvival::classId(cls);
    }
    auto ledger = EbDevRejectLedger::build(in);
    for (auto& note : ledger.notes) {
        if (note.delay < 0 || note.delay > limit || !candReady[note.delay])
            continue;
        auto const& run = cands[note.delay].raw;
        note.detail += fmt::format(" ticks={} died={} deathTick={} unknown={} ballLanding={}",
                                   run.ticksRan, run.died, run.deathTick,
                                   run.unknown || run.unknownTrunc, run.landedAfterAction);
    }
    plan.candNotes = std::move(ledger.notes);
    plan.candNotesTruncated = ledger.truncated;
}

EbSimulationCandidate EbShadowPlanContext::evalDelay(int delayValue, int releaseAfter) {
    EbSimulationCandidate candidate;
    candidate.delay = delayValue;
    if (tickBudget <= 0 || wallExceeded()) {
        plan.budgetHit = true;
        return candidate;
    }
    auto result = runEngine(real, delayValue, releaseAfter, zone.valid ? &zone : nullptr);
    simulator.resetCaptureForNextRun(layer);
    ++plan.candidatesTried;
    candidate.raw = result;
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

bool EbShadowPlanContext::jitterOk(int delay, int releaseAfter) {
    if (!options.robustJitter || delay < 0)
        return true;
    if (tickBudget <= 0 || wallExceeded())
        return true; // soft accept
    static float const kScales[] = {0.97f, 1.03f};
    for (float state : kScales) {
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
        if (!EbShadowSurvivalRules::clearsThreatOf(result, zone) || result.diedBeforeAction)
            return false;
    }
    return true;
}
