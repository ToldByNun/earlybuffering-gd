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

ShadowPlanContext::ShadowPlanContext(ShadowSimulator& simulator, PlayLayer* layer,
                                     PlayerObject* real, bool down, int limit, float deltaTime,
                                     float firstDt, PlanOptions const& options, SimPlan& plan)
    : simulator(simulator), layer(layer), real(real), down(down), limit(limit),
      deltaTime(deltaTime), firstDt(firstDt), options(options), plan(plan),
      model(ShadowSimulator::modelFor(real, limit, options.capMul)),
      robustK(ShadowSurvivalRules::resolveRobustK(real, options)),
      latencyWeight(std::max(0.f, options.latencyWeight)),
      tickBudget(options.tickBudget > 0 ? options.tickBudget : ShadowSimulator::kDefaultTickBudget),
      useTrajEngine(!options.legacyPick &&
                    options.assistEngine == StageConfig::AssistEngineKind::Trajectory) {
    plan.trajectoryEngine = useTrajEngine;
    model.continuePastClear = options.requireAliveForDelay;
    plan.continuous = model.continuous;
    if (StageConfig::assistVerifyAb())
        tickBudget *= 2;
    wall0 = std::chrono::steady_clock::now();
}

bool ShadowPlanContext::wallExceeded() const {
    if (options.budgetUs <= 0)
        return false;
    auto elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - wall0)
                                   .count();
    return elapsedMicroseconds >= options.budgetUs;
}

bool ShadowPlanContext::linkedDual() const {
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

ShadowSimulator::RunResult ShadowPlanContext::runEngine(PlayerObject* who, int delay,
                                                        int releaseAfter, ThreatZone const* z) {
    if (useTrajEngine) {
        return simulator.runTrajectoryOne(layer, who, down, delay, limit, deltaTime, firstDt, model,
                                          releaseAfter, z, &tickBudget);
    }
    return simulator.runOne(layer, who, down, delay, limit, deltaTime, firstDt, model, releaseAfter,
                            z, &tickBudget);
}

void ShadowPlanContext::publishLedger(int chosenDelay, bool doPassFlag,
                                      DevRejectLedger::EarlyExit early) {
    DevRejectLedger::BuildInput in;
    in.limit = limit;
    in.chosenDelay = chosenDelay;
    in.doPass = doPassFlag;
    in.latencyWeight = latencyWeight;
    in.orbIntent = ledgerOrbIntent;
    in.actMin = ledgerActMin;
    in.actMax = ledgerActMax;
    in.legacyPick = options.legacyPick;
    in.earlyExit = early;
    in.includeScores = DevConfig::recordCands();
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
    }
    auto ledger = DevRejectLedger::build(in);
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

SimulationCandidate ShadowPlanContext::evalDelay(int delayValue, int releaseAfter) {
    SimulationCandidate candidate;
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
    candidate.surviveTicks = ShadowSurvivalRules::rankingSurviveTicks(result);
    candidate.surviveBin = ShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
    candidate.requireHold = false; // set only if tap fails and hold-branch wins
    return candidate;
}

void ShadowPlanContext::finalizeViable(SimulationCandidate& candidate, int releaseAfter) {
    candidate.clearsThreat = ShadowSurvivalRules::clearsThreatOf(candidate.raw, zone);
    // clearsThreat + ModeSurvival (soft bake vs Live Delay-worthy Alive).
    candidate.viable =
        ShadowSurvivalRules::runResultDelaySafe(candidate.raw, zone, model, real, requireAlive);
    // Trust-long — alive path must run far enough to see continue
    // (platform after jump). Budget-truncated short runs are not failed here.
    if (candidate.viable && minSurviveLong > 0) {
        bool const truncated = !candidate.raw.died && !candidate.raw.unknownTrunc &&
                               candidate.raw.ticksRan < model.cap &&
                               candidate.raw.ticksRan < minSurviveLong;
        if (!truncated) {
            auto const cls = ModeSurvival::classify(
                ShadowSurvivalRules::survivalInputFrom(candidate.raw, zone, model, real));
            bool const longSafe = requireAlive ? ModeSurvival::countsAsDelayWorthy(cls)
                                               : ModeSurvival::countsAsSafeForDelay(cls);
            if (!AssistHorizon::longPathSuccessOk(candidate.surviveTicks, candidate.raw.died,
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
            if (!ShadowSurvivalRules::runResultDelaySafe(r2, zone, model, other, requireAlive)) {
                candidate.viable = false;
                candidate.clearsThreat = false;
                candidate.dualFailed = true;
            }
        }
    }
}

bool ShadowPlanContext::jitterOk(int delay, int releaseAfter) {
    if (!options.robustJitter || delay < 0)
        return true;
    if (tickBudget <= 0 || wallExceeded())
        return true; // soft accept
    static float const kScales[] = {0.97f, 1.03f};
    for (float state : kScales) {
        float jd = firstDt * state;
        if (jd <= 0.f || jd > deltaTime * 1.5f)
            jd = firstDt;
        ShadowSimulator::RunResult result;
        if (useTrajEngine) {
            result =
                simulator.runTrajectoryOne(layer, real, down, delay, limit, deltaTime, jd, model,
                                           releaseAfter, zone.valid ? &zone : nullptr, &tickBudget);
        } else {
            result = simulator.runOne(layer, real, down, delay, limit, deltaTime, jd, model,
                                      releaseAfter, zone.valid ? &zone : nullptr, &tickBudget);
        }
        simulator.resetCaptureForNextRun(layer);
        if (!ShadowSurvivalRules::clearsThreatOf(result, zone) || result.diedBeforeAction)
            return false;
    }
    return true;
}
