#include "shadow_plan_input_window.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/contracts/live_plan_contracts.hpp"
#include "../../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../../simulation_candidate.hpp"
#include "../idle_prefix_share.hpp"
#include "../shadow_plan_context.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

bool EbShadowPlanInputWindow::evaluate(EbShadowPlanContext& context) {
    if (!context.inputWindow)
        return false;

    context.plan.fixedFireTick = true;
    auto const wasSeeded = [&](int delayValue) -> bool {
        for (auto const& seeded : context.options.seedCandidates) {
            if (seeded.delay == delayValue)
                return true;
        }
        return false;
    };
    auto const complete = [&](EbShadowSimulator::RunResult const& result, int delayValue = -1) {
        bool const unknown = result.unknown || result.unknownTrunc || result.toggleUnknown;
        if (EbMissAssistContracts::survivesObservation(result.died, unknown, result.ticksRan,
                                                       context.limit + context.model.cap,
                                                       result.landedAfterAction)) {
            return true;
        }
        // Adaptive→classic seed: Alive through the paid screen quantum counts.
        if (delayValue >= 0 && wasSeeded(delayValue) && !result.died && !unknown &&
            result.ticksRan > 0) {
            int const quantum = std::max(1, context.options.adaptiveScreenTicks);
            return result.ticksRan >= quantum;
        }
        return false;
    };
    bool const dual = context.linkedDual();
    // Continuous: earliest-safe only (local steering window). Discrete ball/cube
    // must scan the full delay band — earliest Alive is often worse than the
    // survive_then_endx peak (Grief: Live d=2 while Eval bestSurv=5).
    bool const continuous =
        context.real &&
        (context.real->m_isDart || context.real->m_isShip || context.real->m_isSwing);

    context.plan.reason = AssistReason::PassHopeless;
    context.plan.viableDelays.clear();

    int bestDelay = -1;
    int bestSurviveBin = -1;
    float bestScore = 0.f;
    bool bestLanded = false;

    auto considerSafe = [&](int delayValue, EbSimulationCandidate const& candidate) {
        if (delayValue > 0)
            context.plan.viableDelays.push_back(delayValue);

        if (continuous) {
            if (bestDelay >= 0)
                return;
            bestDelay = delayValue;
            return;
        }

        int const surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(candidate.raw);
        int const surviveBin = EbShadowSurvivalRules::surviveBinOf(surviveTicks);
        float const score =
            candidate.endX - context.latencyWeight * static_cast<float>(delayValue);
        bool const landed = candidate.raw.landedAfterAction;

        // Ball stopAtBallLanding makes ticksRan ≈ delay + flight, so later delays
        // inflate surviveBin without surviving longer after the press. Rank landed
        // cands by score only, and require a clear endX gain to prefer a later Delay
        // (Grief Live d=7 vs Eval bestSurv=0).
        if (landed) {
            if (bestDelay < 0) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
                bestLanded = true;
                return;
            }
            if (!bestLanded) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
                bestLanded = true;
                return;
            }
            float const gain = score - bestScore;
            if (gain > EbShadowSimulator::kFutileEndXGain ||
                (std::fabs(gain) <= EbShadowSimulator::kFutileEndXGain &&
                 delayValue < bestDelay)) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
            }
            return;
        }

        if (bestDelay < 0 || surviveBin > bestSurviveBin ||
            (surviveBin == bestSurviveBin && score > bestScore)) {
            bestDelay = delayValue;
            bestSurviveBin = surviveBin;
            bestScore = score;
            bestLanded = false;
        }
    };

    auto ingest = [&](int delayValue, EbSimulationCandidate candidate) -> bool {
        if (candidate.raw.ticksRan == 0) {
            context.plan.budgetHit = true;
            return false;
        }
        bool dead = candidate.raw.died;
        bool safe = complete(candidate.raw, delayValue);
        if (dual && !dead) {
            auto* other = context.real == context.layer->m_player1 ? context.layer->m_player2
                                                                   : context.layer->m_player1;
            if (context.wallExceeded() || context.tickBudget <= 0) {
                context.plan.budgetHit = true;
                return false;
            }
            auto partner = context.runEngine(other, delayValue, -1, nullptr);
            context.simulator.resetCaptureForNextRun(context.layer);
            dead = partner.died;
            safe = safe && complete(partner, delayValue);
        }
        candidate.viable = safe;
        candidate.clearsThreat = safe;
        candidate.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(candidate.raw);
        candidate.surviveBin = EbShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
        context.cands[static_cast<size_t>(delayValue)] = candidate;
        context.candReady[static_cast<size_t>(delayValue)] = 1;
        if (delayValue == 0) {
            context.plan.firstDeathTick = candidate.raw.deathTick;
            context.plan.firstDeathObject = candidate.raw.deathObject;
            if (!dead && !safe) {
                context.plan.reason = AssistReason::PassHardUnknown;
                return false;
            }
        }
        if (safe)
            considerSafe(delayValue, candidate);
        return true;
    };

    // d0 first (unknown / hopeless gates), then shared idle spine for 1..limit.
    if (context.wallExceeded() || context.tickBudget <= 0) {
        context.plan.budgetHit = true;
    } else if (!ingest(0, context.evalDelay(0, -1))) {
        context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                              context.ledgerEarlyExit());
        return true;
    }

    if (!dual && !context.useTrajEngine && context.limit >= 1 && !context.plan.budgetHit &&
        context.plan.reason != AssistReason::PassHardUnknown) {
        EbIdlePrefixShare::evaluateAscending(context);
    }

    for (int delayValue = 1; delayValue <= context.limit; ++delayValue) {
        if (context.plan.reason == AssistReason::PassHardUnknown)
            break;
        if (context.wallExceeded() || context.tickBudget <= 0) {
            context.plan.budgetHit = true;
            break;
        }
        // Peak-death-cut leftovers: do not re-evalDelay (honest Skipped notes).
        if (context.ledgerPeakDeathExit && !context.candReady[static_cast<size_t>(delayValue)])
            break;
        if (context.candReady[static_cast<size_t>(delayValue)]) {
            auto const& ready = context.cands[static_cast<size_t>(delayValue)];
            bool dead = ready.raw.died;
            bool safe = complete(ready.raw, delayValue);
            if (dual && !dead) {
                auto* other = context.real == context.layer->m_player1 ? context.layer->m_player2
                                                                       : context.layer->m_player1;
                if (context.wallExceeded() || context.tickBudget <= 0) {
                    context.plan.budgetHit = true;
                    break;
                }
                auto partner = context.runEngine(other, delayValue, -1, nullptr);
                context.simulator.resetCaptureForNextRun(context.layer);
                dead = partner.died;
                safe = safe && complete(partner, delayValue);
                EbSimulationCandidate updated = ready;
                updated.viable = safe;
                updated.clearsThreat = safe;
                context.cands[static_cast<size_t>(delayValue)] = updated;
            } else {
                EbSimulationCandidate updated = ready;
                updated.viable = safe;
                updated.clearsThreat = safe;
                context.cands[static_cast<size_t>(delayValue)] = updated;
            }
            if (safe)
                considerSafe(delayValue, context.cands[static_cast<size_t>(delayValue)]);
            if (continuous && bestDelay >= 0) {
                context.ledgerEarliestSafeExit = true;
                break;
            }
            continue;
        }

        if (!ingest(delayValue, context.evalDelay(delayValue, -1)))
            break;
        if (continuous && bestDelay >= 0) {
            context.ledgerEarliestSafeExit = true;
            break;
        }
    }

    bool const isWave = context.real && context.real->m_isDart;
    auto const& c0 = context.cands[0];
    bool const d0Safe = context.candReady[0] && c0.viable && !c0.raw.died;

    // AP3b — Wave continuous IW: help-only / proximity / earliest / futile.
    // Classic selection never runs after IW returns, so anti-phase lives here.
    if (isWave && context.down && continuous &&
        context.plan.reason != AssistReason::PassHardUnknown) {
        context.plan.idleSafer = context.simulator.classifyIdleSafer(
            true, true, context.plan.baselineDeathTick >= 0, context.plan.baselineDeathTick,
            context.plan.baselineDeathX, c0.raw.died, c0.raw.died ? c0.raw.deathTick : -1,
            c0.raw.died ? c0.raw.deathX : c0.endX);

        int earliestLater = -1;
        for (int delay = 1; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            if (cand.viable && !cand.raw.died) {
                earliestLater = delay;
                break;
            }
        }
        int const chosen = EbLivePlanContracts::waveIwChooseDelay(d0Safe, earliestLater);

        float const proxX = std::clamp(static_cast<float>(context.limit + 4) * 5.5f,
                                       EbShadowSimulator::kHazardProximityXMin,
                                       EbShadowSimulator::kHazardProximityXMax);
        bool const nearHazard =
            context.simulator.spatialHazardProximity(context.layer, context.real, proxX);
        bool const noOrbTarget =
            !(context.orbAssist &&
              (context.orbHint.found || context.spatialHint || c0.activatedRing ||
               context.plan.foresightOrbPad));

        if (EbLivePlanContracts::waveIwHelpOnlyPass(true, true, d0Safe)) {
            bestDelay = 0;
            context.plan.action = PlanAction::Pass;
            context.plan.delay = context.plan.jumpTick = 0;
            context.plan.nowIsSafe = true;
            context.plan.threatCleared = true;
            if (EbLivePlanContracts::waveIwProximityPass(true, true, c0.raw.died, nearHazard,
                                                         noOrbTarget, context.plan.foresightOrbPad)) {
                context.plan.reason = AssistReason::PassWaveProximity;
                context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::WaveProximity);
                EbAssistLog::debug(fmt::format(
                    "PLAN id={} IW Pass wave-proximity (help-only d0) prox={:.0f}",
                    context.plan.planId, proxX));
                return true;
            }
            context.plan.reason = AssistReason::PassD0Safe;
            context.publishLedger(0, true, context.ledgerEarlyExit());
            return true;
        }

        if (chosen > 0) {
            bestDelay = chosen;
            context.plan.delay = context.plan.jumpTick = chosen;
            context.plan.nowIsSafe = false;
            context.plan.threatCleared = true;
            context.plan.reason = AssistReason::DelayThreat;
            context.plan.action = PlanAction::Delay;
            if (!context.plan.viableDelays.empty()) {
                context.plan.dMin = context.plan.viableDelays.front();
                context.plan.dMax = context.plan.viableDelays.back();
            }
            context.publishLedger(chosen, false, context.ledgerEarlyExit());
            return true;
        }

        int bestBin = 0;
        float bestGain = 0.f;
        float const baseEnd = c0.endX;
        bool anyAliveLater = false;
        for (int delay = 0; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            if (!(cand.viable && !cand.raw.died))
                continue;
            if (cand.surviveBin > bestBin)
                bestBin = cand.surviveBin;
            float const gain = cand.endX - baseEnd;
            if (gain > bestGain)
                bestGain = gain;
            if (delay > 0)
                anyAliveLater = true;
        }
        bool const idleHoldsEscape = context.plan.idleSafer && anyAliveLater;
        bool const orbIntent =
            context.orbAssist &&
            (context.orbHint.found || context.spatialHint || context.plan.foresightOrbPad);
        if (EbLivePlanContracts::waveIwFutilePass(true, true, false, idleHoldsEscape, orbIntent,
                                                  bestBin, bestGain, context.plan.foresightOrbPad)) {
            context.plan.action = PlanAction::Pass;
            context.plan.reason = AssistReason::PassWaveFutile;
            context.plan.delay = context.plan.jumpTick = 0;
            context.plan.nowIsSafe = d0Safe;
            context.plan.threatCleared = !c0.raw.died;
            context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::WaveFutile);
            EbAssistLog::debug(fmt::format(
                "PLAN id={} IW Pass wave-futile bin={} gain={:.1f} idleSafer={}",
                context.plan.planId, bestBin, bestGain, context.plan.idleSafer ? 1 : 0));
            return true;
        }
    }

    if (bestDelay >= 0) {
        context.plan.delay = context.plan.jumpTick = bestDelay;
        context.plan.nowIsSafe = bestDelay == 0;
        context.plan.threatCleared = true;
        context.plan.reason =
            bestDelay ? AssistReason::DelayThreat : AssistReason::PassD0Safe;
        if (bestDelay)
            context.plan.action = PlanAction::Delay;
        else
            context.plan.action = PlanAction::Pass;
        if (!context.plan.viableDelays.empty()) {
            context.plan.dMin = context.plan.viableDelays.front();
            context.plan.dMax = context.plan.viableDelays.back();
        }
    }

    if (context.plan.budgetHit && bestDelay < 0)
        context.plan.reason = AssistReason::PassBudget;

    context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                          context.ledgerEarlyExit());
    return true;
}
