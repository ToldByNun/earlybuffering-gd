#include "shadow_plan_input_window.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/contracts/live_plan_contracts.hpp"
#include "../../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../../../assist/planning/assist_pick_v1.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../../simulation_candidate.hpp"
#include "../idle_prefix_share.hpp"
#include "../shadow_plan_context.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <vector>

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
    auto const knownDeathTick = [&]() -> int {
        // Longest observed death among ready / fully evaluated cands (esp. d0).
        int known = -1;
        if (context.options.seedPrefixDeathTick > 0) {
            known = context.options.seedPrefixDeathTick;
        }
        int const lim = std::max(0, context.limit);
        for (int delay = 0; delay <= lim; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)]) {
                continue;
            }
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            if (!cand.raw.died || cand.raw.deathTick <= 0) {
                continue;
            }
            if (known < 0 || cand.raw.deathTick > known) {
                known = cand.raw.deathTick;
            }
        }
        if (context.plan.firstDeathTick > 0 &&
            (known < 0 || context.plan.firstDeathTick > known)) {
            known = context.plan.firstDeathTick;
        }
        return known;
    };
    auto const observationFloor = [&](int delayValue) -> int {
        int const quantum = std::max(1, context.options.adaptiveScreenTicks);
        if (delayValue >= 0 && wasSeeded(delayValue)) {
            return EbLivePlanContracts::adaptiveHonestAliveMinTicks(quantum, knownDeathTick(),
                                                                    context.model.cap);
        }
        return std::max(1, context.limit + context.model.cap);
    };
    auto const complete = [&](EbShadowSimulator::RunResult const& result, int delayValue = -1) {
        bool const unknown = result.unknown || result.unknownTrunc || result.toggleUnknown;
        if (EbMissAssistContracts::survivesObservation(result.died, unknown, result.ticksRan,
                                                       context.limit + context.model.cap,
                                                       result.landedAfterAction)) {
            return true;
        }
        // Adaptive→classic seed: Alive only if screen quantum AND any known
        // longer death clock are covered (AP10 — no short seed over d0 death).
        if (delayValue >= 0 && wasSeeded(delayValue) && !result.died && !unknown &&
            result.ticksRan > 0) {
            int const quantum = std::max(1, context.options.adaptiveScreenTicks);
            return EbLivePlanContracts::adaptiveSeedCountsAlive(result.ticksRan, quantum,
                                                                knownDeathTick(),
                                                                context.model.cap);
        }
        return false;
    };
    auto const isDepthTruncated = [&](EbShadowSimulator::RunResult const& result,
                                      int delayValue) -> bool {
        bool const unknown = result.unknown || result.unknownTrunc || result.toggleUnknown;
        if (result.landedAfterAction)
            return false;
        return EbLivePlanContracts::depthTruncatedAlive(result.died, unknown, result.ticksRan,
                                                        observationFloor(delayValue));
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
    int bestTicksRan = -1;
    float bestScore = 0.f;
    bool bestLanded = false;
    bool anyDepthTruncated = false;
    int const fairDepthFloor = std::max(1, context.options.adaptiveScreenTicks > 0
                                               ? context.options.adaptiveScreenTicks
                                               : 24);

    auto considerSafe = [&](int delayValue, EbSimulationCandidate const& candidate) {
        if (delayValue > 0)
            context.plan.viableDelays.push_back(delayValue);

        int const surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(candidate.raw);
        int const surviveBin = EbShadowSurvivalRules::surviveBinOf(surviveTicks);
        float const score =
            candidate.endX - context.latencyWeight * static_cast<float>(delayValue);
        bool const landed = candidate.raw.landedAfterAction;
        int const ticksRan = candidate.raw.ticksRan;

        // Assist Pack AP5: continuous (ship/swing) ranks the full survivor band by
        // endX−λ / fair depth — not first-safe. Wave help-only still overrides later.
        if (continuous && EbLivePlanContracts::continuousIwRanksByEndX()) {
            if (bestDelay < 0 ||
                EbLivePlanContracts::fairDepthPreferCand(bestTicksRan, bestSurviveBin, bestScore,
                                                         ticksRan, surviveBin, score,
                                                         fairDepthFloor)) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
                bestTicksRan = ticksRan;
                bestLanded = false;
            }
            return;
        }
        if (continuous) {
            if (bestDelay >= 0)
                return;
            bestDelay = delayValue;
            return;
        }

        // Ball stopAtBallLanding: rank by Eval-aligned score (Assist Pack AP5 /
        // intended landed), not inflated surviveBin.
        if (landed) {
            if (bestDelay < 0) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
                bestTicksRan = ticksRan;
                bestLanded = true;
                return;
            }
            if (!bestLanded) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
                bestTicksRan = ticksRan;
                bestLanded = true;
                return;
            }
            if (EbLivePlanContracts::intendedLandedPreferLater(bestScore, score, bestDelay,
                                                               delayValue)) {
                bestDelay = delayValue;
                bestSurviveBin = surviveBin;
                bestScore = score;
                bestTicksRan = ticksRan;
            }
            return;
        }

        // Assist Pack AP1/AP5: fair depth then endX−λ among discrete survivors.
        if (bestDelay < 0 ||
            EbLivePlanContracts::fairDepthPreferCand(bestTicksRan, bestSurviveBin, bestScore,
                                                     ticksRan, surviveBin, score, fairDepthFloor)) {
            bestDelay = delayValue;
            bestSurviveBin = surviveBin;
            bestScore = score;
            bestTicksRan = ticksRan;
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
        if (!safe && isDepthTruncated(candidate.raw, delayValue))
            anyDepthTruncated = true;
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
            if (!safe && isDepthTruncated(partner, delayValue))
                anyDepthTruncated = true;
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
                // AP4: toggle overlap → named Pass (not opaque HardUnknown).
                context.plan.reason =
                    (candidate.toggleUnknown || context.sawToggleUnknown)
                        ? AssistReason::PassToggleUnknown
                        : AssistReason::PassHardUnknown;
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
            // Assist Pack AP1: shallow Adaptive/prefix Alive must be re-sim'd to
            // the honest floor when budget remains — do not Hopeless on truncated depth.
            if (isDepthTruncated(ready.raw, delayValue) && context.tickBudget > 0 &&
                !context.wallExceeded()) {
                anyDepthTruncated = true;
                context.candReady[static_cast<size_t>(delayValue)] = 0;
                if (!ingest(delayValue, context.evalDelay(delayValue, -1)))
                    break;
                // Assist Pack AP5: no earliest-safe early exit when ranking by endX.
                if (continuous && bestDelay >= 0 &&
                    !EbLivePlanContracts::continuousIwRanksByEndX()) {
                    context.ledgerEarliestSafeExit = true;
                    break;
                }
                continue;
            }
            bool dead = ready.raw.died;
            bool safe = complete(ready.raw, delayValue);
            if (!safe && isDepthTruncated(ready.raw, delayValue))
                anyDepthTruncated = true;
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
                if (!safe && isDepthTruncated(partner, delayValue))
                    anyDepthTruncated = true;
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
            if (continuous && bestDelay >= 0 && !EbLivePlanContracts::continuousIwRanksByEndX()) {
                context.ledgerEarliestSafeExit = true;
                break;
            }
            continue;
        }

        if (!ingest(delayValue, context.evalDelay(delayValue, -1)))
            break;
        if (continuous && bestDelay >= 0 && !EbLivePlanContracts::continuousIwRanksByEndX()) {
            context.ledgerEarliestSafeExit = true;
            break;
        }
    }

    bool const isWave = context.real && context.real->m_isDart;
    auto const& c0 = context.cands[0];
    bool const d0Safe = context.candReady[0] && c0.viable && !c0.raw.died;

    // AP4 — Orb-intent handoff (before Wave help-only / Alive ranking).
    // Classic selection never runs after IW returns — recover G1 earliest±k.
    if (context.orbAssist && context.down &&
        context.plan.reason != AssistReason::PassHardUnknown &&
        context.plan.reason != AssistReason::PassToggleUnknown) {
        bool anyActivation = false;
        int actMin = -1;
        int actMax = -1;
        for (int delay = 0; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto& cand = context.cands[static_cast<size_t>(delay)];
            if (cand.raw.activatedRing)
                cand.activatedRing = true;
            // Observation-safe + ring (or dash-hold salvage already on cand).
            if (!(cand.viable && cand.activatedRing && !cand.raw.died))
                continue;
            anyActivation = true;
            if (actMin < 0)
                actMin = delay;
            actMax = delay;
        }
        bool const orbIntent =
            context.orbHint.found || context.spatialHint || context.plan.foresightOrbPad ||
            anyActivation;
        context.orbIntent = orbIntent;
        context.ledgerOrbIntent = orbIntent;
        if (orbIntent && actMin >= 0) {
            std::vector<EbAssistPickV1::CandView> views(
                static_cast<size_t>(std::max(0, context.limit)) + 1u);
            for (int delay = 0; delay <= context.limit; ++delay) {
                if (!context.candReady[static_cast<size_t>(delay)])
                    continue;
                auto const& cand = context.cands[static_cast<size_t>(delay)];
                views[static_cast<size_t>(delay)].viable = cand.viable && !cand.raw.died;
                views[static_cast<size_t>(delay)].activatedRing = cand.activatedRing;
            }
            auto const pick = EbAssistPickV1::choose(views.data(), context.limit, context.robustK,
                                                     true, actMin, actMax);
            if (pick.chosen > 0 && pick.requireOrb) {
                auto& win = context.cands[static_cast<size_t>(pick.chosen)];
                // Dash hold salvage if tap orb died / not observation-safe.
                if (win.raw.activatedRing && win.raw.wasDashing && !win.viable &&
                    context.tickBudget > 0 && !context.wallExceeded()) {
                    auto hold = context.runEngine(context.real, pick.chosen, 40, nullptr);
                    context.simulator.resetCaptureForNextRun(context.layer);
                    if (!hold.died && complete(hold, pick.chosen)) {
                        win.raw = hold;
                        win.activatedRing = hold.activatedRing;
                        win.viable = true;
                        win.clearsThreat = true;
                        win.requireHold = true;
                        win.releaseAfter = 40;
                        win.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(hold);
                        win.surviveBin = EbShadowSurvivalRules::surviveBinOf(win.surviveTicks);
                        win.endX = hold.endX;
                    }
                }
                if (win.viable && !win.raw.died) {
                    context.plan.action = PlanAction::Delay;
                    context.plan.delay = context.plan.jumpTick = pick.chosen;
                    context.plan.nowIsSafe = false;
                    context.plan.threatCleared = true;
                    context.plan.requireOrb = true;
                    context.plan.activatedRing = true;
                    context.plan.requireHold = win.requireHold;
                    context.plan.releaseAfter = win.releaseAfter;
                    context.plan.dMin = actMin;
                    context.plan.dMax = actMax;
                    context.ledgerActMin = actMin;
                    context.ledgerActMax = actMax;
                    context.plan.reason = EbAssistVerdict::inferDelayReason(
                        true, true, context.hasPrefer);
                    context.publishLedger(pick.chosen, false, context.ledgerEarlyExit());
                    EbAssistLog::debug(fmt::format(
                        "PLAN id={} IW DelayOrb d={} act=[{},{}] hold={} foresight={}",
                        context.plan.planId, pick.chosen, actMin, actMax,
                        context.plan.requireHold ? 1 : 0, context.plan.foresightOrbPad ? 1 : 0));
                    return true;
                }
            }
        }
    }

    // AP3b — Wave continuous IW: help-only / proximity / earliest / futile.
    // Classic selection never runs after IW returns, so anti-phase lives here.
    if (isWave && context.down && continuous &&
        context.plan.reason != AssistReason::PassHardUnknown &&
        context.plan.reason != AssistReason::PassToggleUnknown) {
        context.plan.idleSafer = context.simulator.classifyIdleSafer(
            true, true, context.plan.baselineDeathTick >= 0, context.plan.baselineDeathTick,
            context.plan.baselineDeathX, c0.raw.died, c0.raw.died ? c0.raw.deathTick : -1,
            c0.raw.died ? c0.raw.deathX : c0.endX);

        int earliestLater = -1;
        int bestScoreLater = -1;
        float bestLaterScore = 0.f;
        for (int delay = 1; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            if (!(cand.viable && !cand.raw.died))
                continue;
            if (earliestLater < 0)
                earliestLater = delay;
            float const score =
                cand.endX - context.latencyWeight * static_cast<float>(delay);
            if (bestScoreLater < 0 || score > bestLaterScore) {
                bestScoreLater = delay;
                bestLaterScore = score;
            }
        }
        // Assist Pack AP5: when saving from a dying d0, prefer endX−λ peak.
        int const chosen =
            EbLivePlanContracts::waveIwChooseDelayByScore(d0Safe, bestScoreLater, earliestLater);

        float const proxX = std::clamp(static_cast<float>(context.limit + 4) * 5.5f,
                                       EbShadowSimulator::kHazardProximityXMin,
                                       EbShadowSimulator::kHazardProximityXMax);
        bool const nearHazard =
            context.simulator.spatialHazardProximity(context.layer, context.real, proxX);
        bool const noOrbTarget =
            !(context.orbAssist &&
              (context.orbHint.found || context.spatialHint || c0.activatedRing ||
               context.plan.foresightOrbPad));

        // AP7: seal Wave IW proximity metrics for Dev Lab (not AssistLog-only).
        context.plan.hasWaveMeta = true;
        context.plan.waveProxX = proxX;
        context.plan.waveNearHazard = nearHazard;
        context.plan.waveEarliestLater = earliestLater;

        if (EbLivePlanContracts::waveIwHelpOnlyPass(true, true, d0Safe)) {
            bestDelay = 0;
            context.plan.action = PlanAction::Pass;
            context.plan.delay = context.plan.jumpTick = 0;
            context.plan.nowIsSafe = true;
            context.plan.threatCleared = true;
            context.plan.waveHelpOnly = true;
            if (EbLivePlanContracts::waveIwProximityPass(true, true, c0.raw.died, nearHazard,
                                                         noOrbTarget, context.plan.foresightOrbPad)) {
                context.plan.reason = AssistReason::PassWaveProximity;
                context.plan.waveProximityPass = true;
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
        context.plan.waveBestGain = bestGain;
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
            context.plan.waveFutile = true;
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
    // Assist Pack AP1/AP4: depth-truncated Alive OR dying d0 + any later Alive
    // blocks dishonest PassHopeless (A4 miss → PassBudget / fallthrough).
    if (bestDelay < 0 && context.plan.reason == AssistReason::PassHopeless) {
        bool anyLaterAlive = false;
        for (int delay = 1; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            bool const unknown =
                cand.raw.unknown || cand.raw.unknownTrunc || cand.toggleUnknown;
            if (!cand.raw.died && !unknown && cand.raw.ticksRan > 0) {
                anyLaterAlive = true;
                break;
            }
        }
        bool const d0Died =
            context.candReady[0] && context.cands[0].raw.died && !context.cands[0].raw.unknown;
        if (EbLivePlanContracts::hopelessBlockedByDepthTruncation(anyDepthTruncated, false) ||
            EbLivePlanContracts::hopelessBlockedByLivingLater(d0Died, anyLaterAlive)) {
            context.plan.budgetHit = true;
            context.plan.reason = AssistReason::PassBudget;
            context.plan.action = PlanAction::Pass;
            context.plan.delay = 0;
        }
    }
    // AP4: hopeless with toggle overlap → named PassToggleUnknown.
    if (bestDelay < 0 && context.sawToggleUnknown &&
        (context.plan.reason == AssistReason::PassHopeless ||
         context.plan.reason == AssistReason::PassHardUnknown)) {
        context.plan.reason = AssistReason::PassToggleUnknown;
        context.plan.action = PlanAction::Pass;
        context.plan.delay = 0;
    }

    context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                          context.ledgerEarlyExit());
    return true;
}
