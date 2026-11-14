#include "shadow_plan_input_window.hpp"
#include "../../../../../config/stage_config.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/contracts/live_plan_contracts.hpp"
#include "../../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../../../assist/contracts/orb_probe_order.hpp"
#include "../../../../assist/contracts/subtick_timing.hpp"
#include "../../../../assist/planning/assist_pick_v1.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../../simulation_candidate.hpp"
#include "../../spider_continuation.hpp"
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
        if (context.plan.firstDeathTick > 0 && (known < 0 || context.plan.firstDeathTick > known)) {
            known = context.plan.firstDeathTick;
        }
        return known;
    };
    auto const observationFloor = [&](int delayValue) -> int {
        if (context.real &&
            (context.real->m_isDart || context.real->m_isBird || context.real->m_isSpider ||
             EbStageConfig::kindOf(context.real) == GameModeKind::Cube))
            return context.model.cap;

        int const quantum = std::max(1, context.options.adaptiveScreenTicks);
        if (delayValue >= 0 && wasSeeded(delayValue)) {
            return EbLivePlanContracts::adaptiveHonestAliveMinTicks(quantum, knownDeathTick(),
                                                                    context.model.cap);
        }
        // Assist Pack AP7: prove horizon only — limit+cap was unreachable for
        // discrete cube after Adaptive 96−limit clamp / budget truncation.
        return EbLivePlanContracts::discreteIwObservationFloor(context.model.cap);
    };
    auto const complete = [&](EbShadowSimulator::RunResult const& result, int delayValue = -1) {
        bool const unknown = result.unknown || result.unknownTrunc || result.toggleUnknown;
        if (context.real && context.real->m_isDart) {
            return EbLivePlanContracts::waveTurnVerified(result.died, unknown, result.ticksRan,
                                                         context.model.cap);
        }

        if (EbMissAssistContracts::survivesObservation(result.died, unknown, result.ticksRan,
                                                       observationFloor(delayValue),
                                                       result.landedAfterAction)) {
            return true;
        }
        // Adaptive→classic seed: Alive only if screen quantum AND any known
        // longer death clock are covered (AP10 — no short seed over d0 death).
        if (delayValue >= 0 && wasSeeded(delayValue) && !context.real->m_isBird &&
            !context.real->m_isSpider && !result.died && !unknown && result.ticksRan > 0) {
            int const quantum = std::max(1, context.options.adaptiveScreenTicks);
            return EbLivePlanContracts::adaptiveSeedCountsAlive(
                result.ticksRan, quantum, knownDeathTick(), context.model.cap);
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
    bool const continuous = context.real && (context.real->m_isDart || context.real->m_isShip ||
                                             context.real->m_isSwing);

    context.plan.reason = AssistReason::PassHopeless;
    context.plan.viableDelays.clear();

    int bestDelay = -1;
    int bestSurviveBin = -1;
    int bestTicksRan = -1;
    float bestScore = 0.f;
    bool bestLanded = false;
    bool anyDepthTruncated = false;
    int const fairDepthFloor = std::max(
        1, context.options.adaptiveScreenTicks > 0 ? context.options.adaptiveScreenTicks : 24);

    auto considerSafe = [&](int delayValue, EbSimulationCandidate const& candidate) {
        if (delayValue > 0)
            context.plan.viableDelays.push_back(delayValue);

        int const surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(candidate.raw);
        int const surviveBin = EbShadowSurvivalRules::surviveBinOf(surviveTicks);
        float const score = candidate.endX - context.latencyWeight * static_cast<float>(delayValue);
        bool const landed = candidate.raw.landedAfterAction;
        int const ticksRan = candidate.raw.ticksRan;

        // Assist Pack AP5: continuous (ship/swing) ranks the full survivor band by
        // endX−λ / fair depth — not first-safe. Wave help-only still overrides later.
        if (continuous && EbLivePlanContracts::continuousIwRanksByEndX()) {
            if (bestDelay < 0 || EbLivePlanContracts::fairDepthPreferCand(
                                     bestTicksRan, bestSurviveBin, bestScore, ticksRan, surviveBin,
                                     score, fairDepthFloor)) {
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
        bool safe = complete(candidate.raw, delayValue) || candidate.recoveryPressTick >= 0;
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
            if (!dead && !safe && context.orbAssist && context.orbHint.found &&
                context.real->m_isBird && !candidate.raw.unknown && !candidate.raw.unknownTrunc &&
                !candidate.raw.toggleUnknown) {
                // Intentional baseline screen: leave room for the measured orb
                // window. An incomplete baseline is never considered safe.
                return true;
            }
            if (!dead && !safe) {
                // AP4: toggle overlap → named Pass (not opaque HardUnknown).
                context.plan.reason = (candidate.toggleUnknown || context.sawToggleUnknown)
                                          ? AssistReason::PassToggleUnknown
                                          : AssistReason::PassHardUnknown;
                return false;
            }
        }
        if (safe)
            considerSafe(delayValue, candidate);
        return true;
    };

    bool const isWave = context.real && context.real->m_isDart;

    // AP04: Adaptive seeds already belong to this input and world capture.
    // Reuse completed evaluations, but rebuild shallow Alive results before
    // accepting them at the classic observation floor.
    auto evaluateCandidate = [&](int delayValue) {
        auto const index = static_cast<size_t>(delayValue);
        auto const& cached = context.cands[index].raw;
        bool const finished = cached.died || cached.unknown || cached.unknownTrunc ||
                              cached.toggleUnknown || cached.ticksRan >= context.model.cap;
        if (context.candReady[index] && finished && !isDepthTruncated(cached, delayValue)) {
            ++context.plan.reusedCandidateCount;
            context.plan.reusedCandidateTicks += context.cands[index].raw.ticksRan;
            return context.cands[index];
        }
        context.candReady[index] = 0;
        return context.evalDelay(delayValue, -1);
    };

    // d0 first (unknown / hopeless gates), then shared idle spine for 1..limit.
    if (context.wallExceeded() || context.tickBudget <= 0) {
        context.plan.budgetHit = true;
    } else {
        int const savedCap = context.model.cap;
        bool const orbScreen = context.orbAssist && context.orbHint.found &&
                               context.real->m_isBird && !dual && !context.useTrajEngine;
        if (orbScreen)
            context.model.cap = std::min(savedCap, 12);
        auto baseline = evaluateCandidate(0);
        context.model.cap = savedCap;
        if (!ingest(0, baseline)) {
            context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                                  context.ledgerEarlyExit());
            return true;
        }
    }

    // Assist Pack AP-L4: dying Wave d0 + remUs below continuous reserve → do not
    // burn the rest of the press on a full later-band endX scan; PassBudget later.
    bool skipLaterBand = false;
    if (isWave && continuous && context.down && context.candReady[0]) {
        auto const& d0 = context.cands[0];
        bool const d0DeadTruth =
            d0.raw.died && d0.raw.ticksRan > 0 && !d0.raw.unknown && !d0.raw.unknownTrunc;
        int const laterReserve =
            EbLivePlanContracts::prepareReserveUs(context.options.budgetUs, true);
        bool const laterOk = EbLivePlanContracts::prepareHeavyWorkAllowed(
            context.wallRemUs(), laterReserve, context.options.budgetUs <= 0);
        if (EbLivePlanContracts::waveIwSkipLaterBandForStarve(d0DeadTruth, laterOk)) {
            context.plan.budgetHit = true;
            skipLaterBand = true;
        }
    }

    // Orb scans already measured a reachable activation band. Verify that band
    // before unrelated early flaps/flips consume the wall. Every candidate still
    // needs the normal survival proof; Ball ground/fallback selection stays below.
    if (context.orbAssist && context.orbHint.found && !dual &&
        (context.real->m_isBird || context.real->m_isBall)) {
        auto const order =
            EbOrbProbeOrder::window(context.orbHint.first, context.orbHint.last, context.limit);
        for (int delay : order) {
            if (context.wallExceeded() || context.tickBudget <= 0) {
                context.plan.budgetHit = true;
                break;
            }
            auto candidateResult = context.real->m_isBird && !context.useTrajEngine
                                       ? context.evalDelay(delay, -1, true)
                                       : evaluateCandidate(delay);
            if (!ingest(delay, candidateResult))
                break;
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            if (!candidate.viable || !candidate.raw.activatedRing)
                continue;
            if (context.real->m_isBird)
                skipLaterBand = true;
            break;
        }
    }

    // Assist Pack AP-L6 — AP3c probe-before-band (discrete only).
    // Horizon probe runs after d0 / before 1..limit so mid-band wall spend cannot
    // starve the +24 prove extend (Cataclysm cube sticky PassD0Safe class).
    int savedCapForProbe = context.model.cap;
    bool keepExtendedCapForBand = false;
    bool ranHorizonProbe = false;
    if (!skipLaterBand && !continuous && context.candReady[0] && context.down &&
        context.plan.reason != AssistReason::PassHardUnknown &&
        context.plan.reason != AssistReason::PassToggleUnknown && context.tickBudget > 0 &&
        !context.wallExceeded()) {
        auto const& c0probe = context.cands[0];
        bool const d0AliveProbe = !c0probe.raw.died && !c0probe.raw.unknown &&
                                  !c0probe.raw.unknownTrunc && !c0probe.toggleUnknown;
        int const proveFloor = EbLivePlanContracts::discreteIwObservationFloor(context.model.cap);
        bool const atWall = EbLivePlanContracts::d0AtProveWall(
            d0AliveProbe, c0probe.raw.landedAfterAction, c0probe.raw.ticksRan, proveFloor);
        if (EbLivePlanContracts::discreteIwProbeBeforeBand(
                continuous, EbStageConfig::assistProbePastHorizon(), atWall)) {
            ranHorizonProbe = true;
            context.model.cap = savedCapForProbe + EbLivePlanContracts::kPostHorizonProbeTicks;
            context.candReady[0] = 0;
            if (ingest(0, evaluateCandidate(0))) {
                auto const& probed = context.cands[0];
                if (probed.raw.died) {
                    // Death past prove wall — band must rank later Delay under
                    // the extended cap (restore after the 1..limit scan).
                    bestDelay = -1;
                    bestSurviveBin = -1;
                    bestTicksRan = -1;
                    bestScore = 0.f;
                    bestLanded = false;
                    context.plan.viableDelays.clear();
                    keepExtendedCapForBand = true;
                } else {
                    context.model.cap = savedCapForProbe;
                }
            } else {
                context.model.cap = savedCapForProbe;
            }
        }
    }

    bool const subtickRescue = context.options.allowSubtick && context.real->m_isBird &&
                               context.down && !dual && !context.useTrajEngine &&
                               context.candReady[0] && context.cands[0].raw.died &&
                               !context.cands[0].raw.unknown &&
                               !context.cands[0].raw.unknownTrunc && !context.sawToggleUnknown;
    std::vector<unsigned char> subtickProbed(static_cast<size_t>(context.limit + 1), 0);
    auto refineSubtick = [&](int probeDelay) -> bool {
        if (!subtickRescue || probeDelay < 1 || probeDelay >= context.limit)
            return false;
        for (int index = 0; index < 3; ++index) {
            float const phase = EbSubtickTiming::probePhase(index);
            if (subtickProbed[static_cast<size_t>(probeDelay)] & (1 << index))
                continue;
            if (!EbSubtickTiming::withinWindow(probeDelay, phase, context.limit))
                break;
            if (context.wallExceeded() || context.tickBudget <= 0) {
                context.plan.budgetHit = true;
                break;
            }
            subtickProbed[static_cast<size_t>(probeDelay)] |= (1 << index);
            auto raw = context.simulator.runOne(
                context.layer, context.real, context.down, probeDelay, context.limit,
                context.deltaTime, context.firstDt, context.model, -1,
                context.zone.valid ? &context.zone : nullptr, &context.tickBudget, phase);
            context.simulator.resetCaptureForNextRun(context.layer);
            ++context.plan.candidatesTried;
            if (context.simulator.planWallHit())
                context.plan.budgetHit = true;
            bool const unknown = raw.unknown || raw.unknownTrunc || raw.toggleUnknown;
            if (raw.died || unknown || raw.ticksRan < context.model.cap ||
                (context.orbIntent && !raw.activatedRing))
                continue;
            context.plan.action = PlanAction::Delay;
            context.plan.reason =
                raw.activatedRing ? AssistReason::DelayOrb : AssistReason::DelayThreat;
            context.plan.delay = context.plan.jumpTick = probeDelay;
            context.plan.firePhase = phase;
            context.plan.fixedFireTick = true;
            context.plan.nowIsSafe = false;
            context.plan.threatCleared = true;
            context.plan.activatedRing = raw.activatedRing;
            context.plan.requireOrb = raw.activatedRing;
            context.plan.viableDelays.clear();
            context.plan.guardOriginalDelay = probeDelay;
            context.plan.guardProbeIncomplete = true;
            context.publishLedger(probeDelay, false, context.ledgerEarlyExit());
            for (auto& note : context.plan.candNotes) {
                if (note.delay != probeDelay)
                    continue;
                note.detail = fmt::format("d={} phase={} ticks={} died=false phase-proof=true",
                                          probeDelay, phase, raw.ticksRan);
                note.deathTick = -1;
                note.ticksRan = raw.ticksRan;
                note.unknown = false;
                note.budgetHit = false;
                note.surviveTicks = raw.ticksRan;
                note.survivalClass = "alive";
                note.endX = raw.endX;
                note.surviveBin = EbShadowSurvivalRules::surviveBinOf(raw.ticksRan);
                note.threatCleared = true;
                note.latencyTerm = context.latencyWeight * (probeDelay + phase);
                note.score = raw.endX - note.latencyTerm;
            }
            return true;
        }
        return false;
    };

    // AP03/AP05: probe a useful part of the turn window before the ascending
    // prefix scan spends the wall on early flaps that hit the same obstacle.
    // Keep the existing horizon and unknown checks for every candidate.
    bool const isCube = context.real && EbStageConfig::kindOf(context.real) == GameModeKind::Cube;
    bool const turnRescue = context.real &&
                            (((context.real->m_isBird || context.real->m_isBall ||
                               context.real->m_isSpider || isCube) &&
                              context.down) ||
                             isWave) &&
                            !dual && !context.orbHint.found && !context.spatialHint &&
                            !context.plan.foresightOrbPad && context.limit > 1 &&
                            context.candReady[0] && context.cands[0].raw.died &&
                            !context.cands[0].raw.unknown && !context.cands[0].raw.unknownTrunc &&
                            !context.cands[0].raw.toggleUnknown;
    if (turnRescue) {
        int idleBoundary = -1;
        int const middle = std::max(1, context.limit / 2);
        std::vector<int> probes{middle, context.limit};
        if (isCube) {
            probes.clear();
            for (int index = 0; index < 3; ++index) {
                int const probe = EbLivePlanContracts::cubeRescueProbe(context.limit, index);
                if (probes.empty() || probes.back() != probe)
                    probes.push_back(probe);
            }
        }
        if (context.real->m_isSpider) {
            probes.clear();
            for (int index = 0; index < 4; ++index) {
                int const probe = EbSpiderContinuation::probe(context.limit, index);
                if (std::ranges::find(probes, probe) == probes.end())
                    probes.push_back(probe);
            }
        }
        if (context.real->m_isBall)
            probes.insert(probes.begin(), std::max(1, context.limit / 3));
        if (context.real->m_isBird && !subtickRescue) {
            probes.clear();
            for (int index = 0; index < 3; ++index) {
                int const probe = EbLivePlanContracts::ufoRescueProbe(context.limit, index);
                if (probes.empty() || probes.back() != probe)
                    probes.push_back(probe);
            }
        }
        // One idle probe locates the latest actionable tick. Do not spend the
        // wall on the ascending integer band before refining that boundary.
        if (subtickRescue)
            probes = {middle};
        for (int probe : probes) {
            if (context.wallExceeded() || context.tickBudget <= 0) {
                context.plan.budgetHit = true;
                break;
            }
            auto candidate = evaluateCandidate(probe);
            if (subtickRescue && candidate.raw.diedBeforeAction && !candidate.raw.unknown &&
                !candidate.raw.unknownTrunc && !candidate.raw.toggleUnknown)
                idleBoundary =
                    EbSubtickTiming::ufoBoundaryDelay(candidate.raw.deathTick, context.limit);
            // Spider: a successful teleport can require the player's next click
            // before the full horizon. Prove that continuation with native physics.
            if (context.real->m_isSpider && context.down && !context.useTrajEngine &&
                candidate.raw.died && !candidate.raw.diedBeforeAction && !candidate.raw.unknown &&
                !candidate.raw.unknownTrunc && !candidate.raw.toggleUnknown) {
                for (int lead : {16, 8}) {
                    int const nextPress = candidate.raw.deathTick - lead;
                    if (!EbSpiderContinuation::allowed(context.cands[0].raw.deathTick, probe,
                                                       candidate.raw.deathTick, nextPress))
                        continue;
                    if (context.wallExceeded() || context.tickBudget <= 0)
                        break;
                    auto recoveryModel = context.model;
                    recoveryModel.probeSpiderContinuation = true;
                    auto recovery = context.simulator.runOne(
                        context.layer, context.real, true, probe, context.limit, context.deltaTime,
                        context.firstDt, recoveryModel, nextPress - probe, nullptr,
                        &context.tickBudget);
                    context.simulator.resetCaptureForNextRun(context.layer);
                    ++context.plan.candidatesTried;
                    if (context.simulator.planWallHit())
                        context.plan.budgetHit = true;
                    bool const unknown =
                        recovery.unknown || recovery.unknownTrunc || recovery.toggleUnknown;
                    if (EbSpiderContinuation::verified(recovery.died, unknown, recovery.ticksRan,
                                                       context.model.cap, recovery.followupJumped,
                                                       nextPress)) {
                        candidate.recoveryPressTick = nextPress;
                        candidate.endX = recovery.endX;
                        break;
                    }
                }
            }
            // A delayed release may clear this corner and still need the player's
            // next press. Verify that continuation rather than assuming it works.
            auto const& baseline = context.cands[0].raw;
            if (isWave && !context.down && !context.useTrajEngine && candidate.raw.died &&
                !candidate.raw.diedBeforeAction && !candidate.raw.unknown &&
                !candidate.raw.unknownTrunc && !candidate.raw.toggleUnknown && baseline.died) {
                for (int lead : {8, 12, 16}) {
                    int const nextPress = candidate.raw.deathTick - lead;
                    if (!EbLivePlanContracts::waveRecoveryProbeAllowed(baseline.deathTick, probe,
                                                                       nextPress))
                        continue;
                    if (context.wallExceeded() || context.tickBudget <= 0)
                        break;
                    auto recovery = context.runEngine(context.real, probe, nextPress - probe,
                                                      context.zone.valid ? &context.zone : nullptr);
                    context.simulator.resetCaptureForNextRun(context.layer);
                    ++context.plan.candidatesTried;
                    if (context.simulator.planWallHit())
                        context.plan.budgetHit = true;
                    if (complete(recovery, probe)) {
                        candidate.recoveryPressTick = nextPress;
                        break;
                    }
                }
            }
            if (!ingest(probe, candidate) || (!isWave && bestDelay > 0))
                break;
        }
        if (subtickRescue && bestDelay < 0 && idleBoundary > 0 && !context.wallExceeded() &&
            context.tickBudget > 0) {
            // A partial Alive boundary must get the full proof before unrelated
            // early flaps consume its budget. The same cap/unknown gates apply.
            ingest(idleBoundary, evaluateCandidate(idleBoundary));
            if (bestDelay < 0 && (refineSubtick(idleBoundary) || refineSubtick(idleBoundary - 1)))
                return true;
        }
        // Refine toward the earliest fully verified flap. Keep the verified
        // rescue if the remaining wall expires during refinement.
        int const rescueDelay = bestDelay;
        for (int earlier = 1; !isWave && rescueDelay > 0 && earlier < rescueDelay; ++earlier) {
            if (context.wallExceeded() || context.tickBudget <= 0) {
                context.plan.budgetHit = true;
                break;
            }
            if (!ingest(earlier, evaluateCandidate(earlier)))
                break;
            if (context.cands[static_cast<size_t>(earlier)].viable) {
                bestDelay = earlier;
                break;
            }
        }
        if (!isWave && bestDelay > 0)
            skipLaterBand = true;
    }

    if (!skipLaterBand && !dual && !context.useTrajEngine && context.limit >= 1 &&
        !context.plan.budgetHit && context.plan.reason != AssistReason::PassHardUnknown) {
        EbIdlePrefixShare::evaluateAscending(context);
    }

    for (int delayValue = 1; !skipLaterBand && delayValue <= context.limit; ++delayValue) {
        if (context.plan.reason == AssistReason::PassHardUnknown)
            break;
        // Peak-death-cut leftovers: do not re-evalDelay (honest Skipped notes).
        if (context.ledgerPeakDeathExit && !context.candReady[static_cast<size_t>(delayValue)])
            continue;
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
            bool safe = complete(ready.raw, delayValue) || ready.recoveryPressTick >= 0;
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

        // Completed prefix/Adaptive results above remain usable after the
        // simulation deadline. Only new physics work needs remaining budget.
        if (context.wallExceeded() || context.tickBudget <= 0) {
            context.plan.budgetHit = true;
            continue;
        }
        if (!ingest(delayValue, context.evalDelay(delayValue, -1)))
            break;
        if (continuous && bestDelay >= 0 && !EbLivePlanContracts::continuousIwRanksByEndX()) {
            context.ledgerEarliestSafeExit = true;
            break;
        }
    }

    if (keepExtendedCapForBand)
        context.model.cap = savedCapForProbe;

    bool const isBall = context.real && context.real->m_isBall;
    bool groundSearchComplete = true;
    bool hasStableLanding = false;
    for (int delay = 0; isBall && delay <= context.limit; ++delay) {
        auto const index = static_cast<size_t>(delay);
        if (!context.candReady[index] || isDepthTruncated(context.cands[index].raw, delay)) {
            groundSearchComplete = false;
            continue;
        }
        auto const& candidate = context.cands[index];
        hasStableLanding =
            hasStableLanding ||
            (candidate.viable && candidate.raw.landedAfterAction && !candidate.raw.died);
    }
    bool const ballOrbFallback = EbLivePlanContracts::ballOrbFallbackAllowed(
        isBall, context.orbAssist, hasStableLanding, groundSearchComplete);
    // A Ball input in flight can be aligned to an orb only after the normal
    // landing search found no stable surface. The scan shares the input budget.
    if (ballOrbFallback && !dual && !context.useTrajEngine && !context.wallExceeded() &&
        context.tickBudget > 0) {
        auto window = context.simulator.scanOrbWindow(context.layer, context.real, context.down,
                                                      context.limit + 1, context.deltaTime,
                                                      context.firstDt, &context.tickBudget, false);
        context.simulator.resetCaptureForNextRun(context.layer);
        if (window.found) {
            context.orbHint = window;
            context.plan.usedOrbWindow = true;
            for (int delay = std::max(0, window.first);
                 delay <= std::min(context.limit, window.last); ++delay) {
                if (context.wallExceeded() || context.tickBudget <= 0)
                    break;
                // Orb activation and the existing full survival test both apply.
                ingest(delay, evaluateCandidate(delay));
            }
        }
        if (context.simulator.planWallHit())
            context.plan.budgetHit = true;
    }

    auto const& c0 = context.cands[0];
    bool const d0Safe = context.candReady[0] && c0.viable && !c0.raw.died;
    bool const d0HasRunTruth = EbLivePlanContracts::waveIwD0HasRunTruth(
        context.candReady[0] != 0, context.candReady[0] ? c0.raw.ticksRan : 0);

    // AP4 — Orb-intent handoff (before Wave help-only / Alive ranking).
    // Classic selection never runs after IW returns — recover G1 earliest±k.
    if (context.orbAssist && context.down && (!isBall || ballOrbFallback) &&
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
        bool const orbIntent = context.orbHint.found || context.spatialHint ||
                               context.plan.foresightOrbPad || anyActivation;
        context.orbIntent = orbIntent;
        context.ledgerOrbIntent = orbIntent;
        if (orbIntent && actMin >= 0) {
            std::vector<EbAssistPickV1::CandView> views(
                static_cast<size_t>(std::max(0, context.limit)) + 1u);
            for (int delay = 0; delay <= context.limit; ++delay) {
                if (!context.candReady[static_cast<size_t>(delay)])
                    continue;
                auto const& cand = context.cands[static_cast<size_t>(delay)];
                views[static_cast<size_t>(delay)].viable =
                    cand.viable && !cand.raw.died && (!isBall || cand.activatedRing);
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
                    if (win.raw.orbContinuationTick >= 0) {
                        // The nominal proof includes the next player orb press.
                        // Integer jitter probes must not silently omit it.
                        context.plan.guardOriginalDelay = pick.chosen;
                        context.plan.guardProbeIncomplete = true;
                        context.plan.viableDelays = {pick.chosen};
                        actMin = actMax = pick.chosen;
                    }
                    context.plan.requireHold = win.requireHold;
                    context.plan.releaseAfter = win.releaseAfter;
                    context.plan.dMin = actMin;
                    context.plan.dMax = actMax;
                    context.ledgerActMin = actMin;
                    context.ledgerActMax = actMax;
                    context.plan.reason =
                        EbAssistVerdict::inferDelayReason(true, true, context.hasPrefer);
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
    if (isWave && continuous && context.plan.reason != AssistReason::PassHardUnknown &&
        context.plan.reason != AssistReason::PassToggleUnknown) {
        // Assist Pack AP-L4: unevaluated / starved Wave → PassBudget, never fake futile.
        if (!d0HasRunTruth) {
            context.plan.action = PlanAction::Pass;
            context.plan.reason = AssistReason::PassBudget;
            context.plan.budgetHit = true;
            context.plan.delay = context.plan.jumpTick = 0;
            context.plan.nowIsSafe = false;
            context.plan.threatCleared = false;
            context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::Budget);
            EbAssistLog::debug(fmt::format(
                "PLAN id={} IW PassBudget wave-starved (no d0 run truth)", context.plan.planId));
            return true;
        }

        context.plan.idleSafer = context.simulator.classifyIdleSafer(
            true, context.down, context.plan.baselineDeathTick >= 0, context.plan.baselineDeathTick,
            context.plan.baselineDeathX, c0.raw.died, c0.raw.died ? c0.raw.deathTick : -1,
            c0.raw.died ? c0.raw.deathX : c0.endX);

        int earliestLater = -1;
        int bestScoreLater = -1;
        float bestLaterScore = 0.f;
        for (int delay = 1; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            if (!(cand.viable && (!cand.raw.died || cand.recoveryPressTick >= 0)))
                continue;
            if (earliestLater < 0)
                earliestLater = delay;
            float const score = cand.endX - context.latencyWeight * static_cast<float>(delay);
            if (bestScoreLater < 0 || score > bestLaterScore) {
                bestScoreLater = delay;
                bestLaterScore = score;
            }
        }
        // Assist Pack AP5: when saving from a dying d0, prefer endX−λ peak.
        int chosen =
            EbLivePlanContracts::waveIwChooseDelayByScore(d0Safe, bestScoreLater, earliestLater);
        // Prefer the earliest verified interior tick of a safe window. A
        // boundary-only turn has no timing margin even if its ghost survives.
        for (int delay = 2; !d0Safe && delay < context.limit; ++delay) {
            auto verified = [&](int candidateDelay) {
                auto const index = static_cast<size_t>(candidateDelay);
                return context.candReady[index] && context.cands[index].viable &&
                       complete(context.cands[index].raw, candidateDelay);
            };
            if (EbLivePlanContracts::waveTurnHasMargin(verified(delay - 1), verified(delay),
                                                       verified(delay + 1))) {
                chosen = delay;
                break;
            }
        }

        float const proxX = std::clamp(static_cast<float>(context.limit + 4) * 5.5f,
                                       EbShadowSimulator::kHazardProximityXMin,
                                       EbShadowSimulator::kHazardProximityXMax);
        bool const nearHazard =
            context.simulator.spatialHazardProximity(context.layer, context.real, proxX);
        bool const noOrbTarget =
            !(context.orbAssist && (context.orbHint.found || context.spatialHint ||
                                    c0.activatedRing || context.plan.foresightOrbPad));

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
                                                         noOrbTarget,
                                                         context.plan.foresightOrbPad)) {
                context.plan.reason = AssistReason::PassWaveProximity;
                context.plan.waveProximityPass = true;
                context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::WaveProximity);
                EbAssistLog::debug(
                    fmt::format("PLAN id={} IW Pass wave-proximity (help-only d0) prox={:.0f}",
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

        // Assist Pack AP-M1: lethal later that still progresses past dying d0
        // (later deathTick / ticks / endX) → Delay, never PassWaveFutile.
        bool anyLaterProgress = false;
        int laterReadyCount = 0;
        if (c0.raw.died) {
            for (int delay = 1; delay <= context.limit; ++delay) {
                if (!context.candReady[static_cast<size_t>(delay)])
                    continue;
                ++laterReadyCount;
                auto const& cand = context.cands[static_cast<size_t>(delay)];
                if (!EbLivePlanContracts::waveIwLaterProgressBeatsD0(
                        true, c0.raw.deathTick, c0.raw.ticksRan, c0.endX, true, cand.raw.died,
                        cand.raw.deathTick, cand.raw.ticksRan, cand.endX))
                    continue;
                anyLaterProgress = true;
            }
        } else {
            for (int delay = 1; delay <= context.limit; ++delay) {
                if (context.candReady[static_cast<size_t>(delay)])
                    ++laterReadyCount;
            }
        }
        // A later collision alone does not prove that a player can recover.
        // Keep the diagnostic progress evidence, but only verified survivors arm.

        int bestBin = 0;
        float bestGain = 0.f;
        float const baseEnd = c0.endX;
        bool anyAliveLater = false;
        for (int delay = 0; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            if (!(cand.viable && (!cand.raw.died || cand.recoveryPressTick >= 0)))
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
        bool const anyAliveSaveFromD0Death = c0.raw.died && anyAliveLater;
        bool const idleHoldsEscape = context.plan.idleSafer && anyAliveLater;
        bool const orbIntent = context.orbAssist && (context.orbHint.found || context.spatialHint ||
                                                     context.plan.foresightOrbPad);
        // AP-M1: incomplete later band without budgetHit → PassBudget, not futile.
        if (EbLivePlanContracts::waveIwPreferPassBudgetOverFutile(
                d0HasRunTruth, context.plan.budgetHit, laterReadyCount, context.limit)) {
            context.plan.action = PlanAction::Pass;
            context.plan.reason = AssistReason::PassBudget;
            context.plan.budgetHit = true;
            context.plan.delay = context.plan.jumpTick = 0;
            context.plan.nowIsSafe = false;
            context.plan.threatCleared = false;
            context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::Budget);
            EbAssistLog::debug(
                fmt::format("PLAN id={} IW PassBudget wave-incomplete-band ready={}/{}",
                            context.plan.planId, laterReadyCount, context.limit));
            return true;
        }
        if (EbLivePlanContracts::waveIwFutilePass(true, true, anyAliveSaveFromD0Death,
                                                  idleHoldsEscape, orbIntent, bestBin, bestGain,
                                                  context.plan.foresightOrbPad, anyLaterProgress)) {
            context.plan.action = PlanAction::Pass;
            context.plan.delay = context.plan.jumpTick = 0;
            // Assist Pack AP-L4: budget/wall starve mid-band is PassBudget, not futile.
            if (EbLivePlanContracts::waveIwFutileHonest(d0HasRunTruth, context.plan.budgetHit)) {
                context.plan.reason = AssistReason::PassWaveFutile;
                context.plan.nowIsSafe = d0Safe;
                context.plan.threatCleared = !c0.raw.died;
                context.plan.waveFutile = true;
                context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::WaveFutile);
                EbAssistLog::debug(fmt::format(
                    "PLAN id={} IW Pass wave-futile bin={} gain={:.1f} idleSafer={}",
                    context.plan.planId, bestBin, bestGain, context.plan.idleSafer ? 1 : 0));
            } else {
                context.plan.reason = AssistReason::PassBudget;
                context.plan.budgetHit = true;
                context.plan.nowIsSafe = false;
                context.plan.threatCleared = false;
                context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::Budget);
                EbAssistLog::debug(fmt::format(
                    "PLAN id={} IW PassBudget wave-starved (futile shape without honest band)",
                    context.plan.planId));
            }
            return true;
        }
    }

    // Assist Pack AP3c — Micro-Window (post-band). Probe Past Horizon runs
    // AP-L6 probe-before-band above so wall-starved bands cannot skip it.
    // Discrete only: Wave/ship keep their own help-only / earliest paths above.
    if (!continuous && bestDelay == 0 && context.candReady[0]) {
        auto const& c0ref = context.cands[0];
        bool const d0AliveNow = !c0ref.raw.died && !c0ref.raw.unknown && !c0ref.raw.unknownTrunc &&
                                !c0ref.toggleUnknown;

        // Micro-Window: tiny Max Assist → any endX lead may beat Pass-now.
        if (d0AliveNow && EbStageConfig::assistMicroWindow()) {
            int microBest = -1;
            float microEnd = c0ref.endX;
            for (int delay = 1; delay <= context.limit; ++delay) {
                if (!context.candReady[static_cast<size_t>(delay)])
                    continue;
                auto const& cand = context.cands[static_cast<size_t>(delay)];
                bool const unknown =
                    cand.raw.unknown || cand.raw.unknownTrunc || cand.toggleUnknown;
                if (cand.raw.died || unknown || cand.raw.ticksRan <= 0)
                    continue;
                if (!EbLivePlanContracts::microWindowPreferLater(true, context.limit, cand.endX,
                                                                 c0ref.endX))
                    continue;
                if (microBest < 0 || cand.endX > microEnd) {
                    microBest = delay;
                    microEnd = cand.endX;
                }
            }
            if (microBest > 0)
                bestDelay = microBest;
        }

        // Assist Pack AP-M3: AP-L6 probe can leave d0 Alive past observation
        // floor (+24) while the later band is restored to floor — probe-inflated
        // endX sticky PassD0Safe. Prefer best floor-reached later by endX−λ
        // (micro does not apply at ticks-all=12). Shallow under-horizon alone
        // still does not demote (AP3).
        if (ranHorizonProbe && bestDelay == 0 && d0AliveNow) {
            int const obsFloor = EbLivePlanContracts::discreteIwObservationFloor(savedCapForProbe);
            bool const probeExt = EbLivePlanContracts::discreteIwProbeExtendedD0Alive(
                d0AliveNow, c0ref.raw.ticksRan, obsFloor, true);
            if (probeExt) {
                int bandBest = -1;
                float bandScore = 0.f;
                for (int delay = 1; delay <= context.limit; ++delay) {
                    if (!context.candReady[static_cast<size_t>(delay)])
                        continue;
                    auto const& cand = context.cands[static_cast<size_t>(delay)];
                    bool const unknown =
                        cand.raw.unknown || cand.raw.unknownTrunc || cand.toggleUnknown;
                    if (cand.raw.died || unknown || cand.raw.ticksRan <= 0)
                        continue;
                    bool const atFloor = cand.raw.ticksRan >= obsFloor;
                    if (!EbLivePlanContracts::discreteIwLivingBandDemotesPassD0Safe(true, true,
                                                                                    atFloor))
                        continue;
                    float const score =
                        cand.endX - context.latencyWeight * static_cast<float>(delay);
                    if (bandBest < 0 || score > bandScore) {
                        bandBest = delay;
                        bandScore = score;
                    }
                }
                if (bandBest > 0)
                    bestDelay = bandBest;
            }
        }

        // Fallback probe only if pre-band probe did not run (setting flipped /
        // landed-not-wall path) and band still left remUs — preserves AP3c when
        // later Alive appears only after the band.
        if (!ranHorizonProbe && bestDelay == 0) {
            int const proveFloor =
                EbLivePlanContracts::discreteIwObservationFloor(context.model.cap);
            bool const atWall = EbLivePlanContracts::d0AtProveWall(
                d0AliveNow, c0ref.raw.landedAfterAction, c0ref.raw.ticksRan, proveFloor);
            bool anyLaterAlive = false;
            for (int delay = 1; delay <= context.limit; ++delay) {
                if (!context.candReady[static_cast<size_t>(delay)])
                    continue;
                auto const& cand = context.cands[static_cast<size_t>(delay)];
                bool const unknown =
                    cand.raw.unknown || cand.raw.unknownTrunc || cand.toggleUnknown;
                if (!cand.raw.died && !unknown && cand.raw.ticksRan > 0)
                    anyLaterAlive = true;
            }
            if (EbLivePlanContracts::horizonProbeBlocksPassD0Safe(
                    EbStageConfig::assistProbePastHorizon(), atWall, anyLaterAlive) &&
                context.tickBudget > 0 && !context.wallExceeded()) {
                int const savedCap = context.model.cap;
                context.model.cap = savedCap + EbLivePlanContracts::kPostHorizonProbeTicks;
                context.candReady[0] = 0;
                if (ingest(0, evaluateCandidate(0))) {
                    auto const& probed = context.cands[0];
                    if (probed.raw.died) {
                        bestDelay = -1;
                        bestSurviveBin = -1;
                        bestTicksRan = -1;
                        bestScore = 0.f;
                        bestLanded = false;
                        context.plan.viableDelays.clear();
                        for (int delay = 1; delay <= context.limit; ++delay) {
                            if (context.wallExceeded() || context.tickBudget <= 0) {
                                context.plan.budgetHit = true;
                                break;
                            }
                            if (!context.candReady[static_cast<size_t>(delay)])
                                continue;
                            auto const& ready = context.cands[static_cast<size_t>(delay)];
                            bool const unknown =
                                ready.raw.unknown || ready.raw.unknownTrunc || ready.toggleUnknown;
                            if (ready.raw.died || unknown)
                                continue;
                            context.candReady[static_cast<size_t>(delay)] = 0;
                            if (!ingest(delay, context.evalDelay(delay, -1)))
                                break;
                        }
                    }
                }
                context.model.cap = savedCap;
            }
        }
    }

    // Last-chance refinement also covers inputs without an idle-boundary probe.
    if (bestDelay < 0 && subtickRescue) {
        int probeDelay = -1;
        int longestLife = -1;
        for (int delay = 1; delay < context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            if (candidate.raw.diedBeforeAction || candidate.raw.unknown ||
                candidate.raw.unknownTrunc || candidate.raw.toggleUnknown)
                continue;
            if (candidate.raw.died && candidate.raw.deathTick > longestLife) {
                longestLife = candidate.raw.deathTick;
                probeDelay = delay;
            }
        }
        if (refineSubtick(probeDelay))
            return true;
    }

    if (bestDelay >= 0) {
        context.plan.delay = context.plan.jumpTick = bestDelay;
        context.plan.nowIsSafe = bestDelay == 0;
        context.plan.threatCleared = true;
        context.plan.reason = bestDelay ? AssistReason::DelayThreat : AssistReason::PassD0Safe;
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
    // Assist Pack AP1/AP4/AP-M4: depth-truncated Alive, dying d0 + later run-alive
    // (incl. ball landed), or incomplete ball band → PassBudget, never Hopeless.
    if (bestDelay < 0 && context.plan.reason == AssistReason::PassHopeless) {
        bool anyLaterRunAlive = false;
        for (int delay = 1; delay <= context.limit; ++delay) {
            if (!context.candReady[static_cast<size_t>(delay)])
                continue;
            auto const& cand = context.cands[static_cast<size_t>(delay)];
            bool const unknown = cand.raw.unknown || cand.raw.unknownTrunc || cand.toggleUnknown;
            // Do not skip landedAfterAction — stopAtBallLanding peers still count.
            if (EbLivePlanContracts::discreteIwLaterRunAliveForHopeless(cand.raw.died, unknown,
                                                                        cand.raw.ticksRan)) {
                anyLaterRunAlive = true;
                break;
            }
        }
        bool const d0Died =
            context.candReady[0] && context.cands[0].raw.died && !context.cands[0].raw.unknown;
        bool const isBall = context.real && context.real->m_isBall;
        if (EbLivePlanContracts::discreteIwHopelessMustFallthrough(
                d0Died, anyLaterRunAlive, anyDepthTruncated, isBall, context.plan.budgetHit,
                context.plan.candidatesTried, context.limit)) {
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

    if (context.real->m_isSpider && bestDelay > 0 &&
        context.cands[static_cast<size_t>(bestDelay)].recoveryPressTick >= 0) {
        // Keep this verified timing paired with its continuation proof. Neither
        // learning nor single-click jitter may substitute a different delay.
        context.plan.guardOriginalDelay = bestDelay;
        context.plan.guardProbeIncomplete = true;
        context.plan.viableDelays = {bestDelay};
        context.plan.dMin = context.plan.dMax = bestDelay;
    }
    context.publishLedger(context.plan.delay, context.plan.action == PlanAction::Pass,
                          context.ledgerEarlyExit());
    return true;
}
