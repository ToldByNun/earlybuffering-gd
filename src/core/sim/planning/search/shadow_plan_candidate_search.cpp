#include "shadow_plan_candidate_search.hpp"
#include "../../../../debug/assist_log.hpp"
#include "../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../prefix_sim.hpp"
#include "../shadow_survival_rules.hpp"
#include "../simulation_candidate.hpp"
#include "shadow_plan_context.hpp"
#include <algorithm>
#include <vector>

void ShadowPlanCandidateSearch::evaluate(ShadowPlanContext& context) {
    std::vector<int> order;
    order.reserve(static_cast<size_t>(context.limit) + 1);
    order.push_back(0);
    auto pushUnique = [&](int delay) {
        if (delay < 0 || delay > context.limit)
            return;
        if (std::find(order.begin(), order.end(), delay) == order.end())
            order.push_back(delay);
    };
    if (context.orbHint.found) {
        for (int delay = context.orbHint.first; delay <= context.orbHint.last; ++delay)
            pushUnique(delay);
        for (int delay = std::max(0, context.orbHint.first - 2);
             delay <= std::min(context.limit, context.orbHint.last + 2); ++delay) {
            pushUnique(delay);
        }
    }

    // Bake-prior prefer band (after d0 / orb): evaluate ±k margin so robust check works.
    context.preferLo = -1;
    context.preferHi = -1;
    context.hasPrefer = context.options.preferDelayMin >= 0 &&
                        context.options.preferDelayMax >= context.options.preferDelayMin;
    if (context.hasPrefer) {
        context.preferLo = std::max(0, context.options.preferDelayMin);
        context.preferHi = std::min(context.limit, context.options.preferDelayMax);
        context.hasPrefer = context.preferLo <= context.preferHi;
    }
    int preferLastInOrder = -1;
    if (context.hasPrefer) {
        int const offset = context.robustK;
        int const marginLo = std::max(0, context.preferLo - offset);
        int const marginHi = std::min(context.limit, context.preferHi + offset);
        for (int delay = marginLo; delay <= marginHi; ++delay)
            pushUnique(delay);
        for (int delay : order) {
            if (delay >= marginLo && delay <= marginHi)
                preferLastInOrder = delay;
        }
    }

    for (int delay = 1; delay <= context.limit; ++delay)
        pushUnique(delay);

    auto preferBandRobust = [&]() -> bool {
        if (!context.hasPrefer)
            return false;
        int const offset = context.robustK;
        for (int delay = context.preferLo; delay <= context.preferHi; ++delay) {
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            if (!candidate.viable)
                continue;
            bool ok = true;
            for (int entry = delay - offset; entry <= delay + offset; ++entry) {
                if (entry < 0 || entry > context.limit)
                    continue;
                if (!context.cands[static_cast<size_t>(entry)].viable) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return true;
        }
        for (int delay = context.preferLo; delay <= context.preferHi; ++delay) {
            if (context.cands[static_cast<size_t>(delay)].viable)
                return true;
        }
        return false;
    };

    // Commit 10 PrefixSim: shared-prefix early-death + same-crash cut.
    int prefixDeathTick = -1;
    int sameCrashStreak = 0;
    int sameCrashPrevTick = -1;
    bool sameCrashStopped = false;

    bool const isWaveMode = context.real && context.real->m_isDart;
    bool const isShipLike = context.real && (context.real->m_isShip || context.real->m_isBird ||
                                             context.real->m_isSpider || context.real->m_isSwing);
    bool const isRobot = context.real && context.real->m_isRobot;

    for (int delayValue : order) {
        if (delayValue == 0)
            continue;
        if (delayValue >= 0 && delayValue <= context.limit &&
            context.candReady[static_cast<size_t>(delayValue)]) {
            continue; // already filled by OpportunityProbe
        }
        if (context.tickBudget <= 0 || context.wallExceeded()) {
            context.plan.budgetHit = true;
            // Remaining delays stay unevaluated → budget-not-reached in ledger.
            break;
        }

        // Shared prefix: idle death at T ⇒ skip every delay d > T (same fate).
        if (PrefixSim::shouldSkipAfterPrefixDeath(delayValue, prefixDeathTick)) {
            SimulationCandidate skip;
            skip.delay = delayValue;
            skip.diedBeforeAction = true;
            skip.raw.died = true;
            skip.raw.diedBeforeAction = true;
            skip.raw.deathTick = prefixDeathTick;
            context.finalizeViable(skip, -1);
            context.cands[static_cast<size_t>(delayValue)] = skip;
            context.candReady[static_cast<size_t>(delayValue)] = 1;
            continue;
        }

        auto candidate = context.evalDelay(delayValue, -1);
        if (candidate.diedBeforeAction) {
            prefixDeathTick =
                PrefixSim::mergePrefixDeathTick(prefixDeathTick, candidate.raw.deathTick, true);
            context.finalizeViable(candidate, -1);
            context.cands[static_cast<size_t>(delayValue)] = candidate;
            if (delayValue >= 0 && delayValue <= context.limit) {
                context.candReady[static_cast<size_t>(delayValue)] = 1;
            }
            // Fill remaining d > T without more sims.
            if (prefixDeathTick >= 0) {
                for (int sd : order) {
                    if (sd <= prefixDeathTick)
                        continue;
                    if (sd < 0 || sd > context.limit)
                        continue;
                    if (context.candReady[static_cast<size_t>(sd)])
                        continue;
                    SimulationCandidate fill;
                    fill.delay = sd;
                    fill.diedBeforeAction = true;
                    fill.raw.died = true;
                    fill.raw.diedBeforeAction = true;
                    fill.raw.deathTick = prefixDeathTick;
                    context.finalizeViable(fill, -1);
                    context.cands[static_cast<size_t>(sd)] = fill;
                    context.candReady[static_cast<size_t>(sd)] = 1;
                }
            }
            continue;
        }

        // Quick pass only here; hold-branch deferred to two-pass full-score set.
        context.finalizeViable(candidate,
                               candidate.releaseAfter >= 0 ? candidate.releaseAfter : -1);
        context.cands[static_cast<size_t>(delayValue)] = candidate;
        if (delayValue >= 0 && delayValue <= context.limit) {
            context.candReady[static_cast<size_t>(delayValue)] = 1;
        }

        // Same deathTick after press across delays → next obstacle, stop.
        sameCrashStreak = PrefixSim::updateSameCrashStreak(
            sameCrashStreak, sameCrashPrevTick, candidate.raw.deathTick, candidate.raw.died,
            candidate.diedBeforeAction);
        if (candidate.raw.died && !candidate.diedBeforeAction && candidate.raw.deathTick >= 0) {
            sameCrashPrevTick = candidate.raw.deathTick;
        }
        if (PrefixSim::sameCrashShouldStop(sameCrashStreak)) {
            sameCrashStopped = true;
            AssistLog::debug(fmt::format("PLAN id={} same-crash stop deathTick={} after delay={}",
                                         context.plan.planId, candidate.raw.deathTick, delayValue));
            break;
        }

        // Early-exit once prefer±k margin is evaluated and a robust prefer delay exists.
        // v1 only (New choose removed): orb foresight still blocks exit so activations
        // outside prefer stay searchable.
        bool const preferExitOk = MissAssistContracts::preferBandEarlyExitAllowed(
            context.orbAssist, context.orbHint.found, context.spatialHint,
            context.plan.foresightOrbPad);
        if (context.hasPrefer && preferExitOk && !context.plan.budgetHit &&
            delayValue == preferLastInOrder && preferBandRobust()) {
            AssistLog::debug(fmt::format("PLAN id={} prefer-band early-exit [{},{}] after {} cands",
                                         context.plan.planId, context.preferLo, context.preferHi,
                                         context.plan.candidatesTried));
            context.ledgerPreferEarlyExit = true;
            break;
        }
        if (context.hasPrefer && !preferExitOk && delayValue == preferLastInOrder &&
            preferBandRobust()) {
            AssistLog::debug(
                fmt::format("PLAN id={} prefer-band early-exit suppressed (orb foresight) [{},{}]",
                            context.plan.planId, context.preferLo, context.preferHi));
        }
    }

    // Commit 10 two-pass: expensive hold-branch / release theories only on full-score set.
    {
        std::vector<PrefixSim::QuickSample> quick;
        quick.reserve(static_cast<size_t>(context.limit) + 1);
        for (int delayValue = 0; delayValue <= context.limit; ++delayValue) {
            if (!context.candReady[static_cast<size_t>(delayValue)])
                continue;
            auto const& candidate = context.cands[static_cast<size_t>(delayValue)];
            PrefixSim::QuickSample s;
            s.delay = delayValue;
            s.died = candidate.raw.died;
            s.diedBeforeAction = candidate.diedBeforeAction;
            s.viable = candidate.viable;
            s.activatedRing = candidate.activatedRing;
            s.deathTick = candidate.raw.deathTick;
            s.surviveTicks = candidate.surviveTicks;
            s.endX = candidate.endX;
            quick.push_back(s);
        }
        auto const fullDelays = PrefixSim::delaysForFullScore(
            quick.data(), static_cast<int>(quick.size()), PrefixSim::kFullScoreTopK);
        bool const holdTheoryOk = PrefixSim::allowHoldBranchTheory(context.model.continuous,
                                                                   isRobot, isWaveMode, isShipLike);

        for (int delayValue : fullDelays) {
            if (delayValue < 0 || delayValue > context.limit)
                continue;
            if (context.tickBudget <= 0 || context.wallExceeded()) {
                context.plan.budgetHit = true;
                break;
            }
            auto& candidate = context.cands[static_cast<size_t>(delayValue)];

            // Mode-gated release branch (WaveTurn / ship recover / robot).
            if (candidate.raw.died && !candidate.diedBeforeAction && context.down && holdTheoryOk &&
                context.options.maxBranches > 0) {
                int tried = 0;
                for (int offset : ShadowSurvivalRules::BRANCH_RELEASES) {
                    if (tried >= context.options.maxBranches)
                        break;
                    if (context.tickBudget <= 0)
                        break;
                    ++tried;
                    auto br = context.runEngine(context.real, delayValue, offset,
                                                context.zone.valid ? &context.zone : nullptr);
                    context.simulator.resetCaptureForNextRun(context.layer);
                    if (!br.died || br.unknownTrunc) {
                        candidate.raw = br;
                        candidate.activatedRing = br.activatedRing;
                        candidate.unknownTrunc = br.unknownTrunc;
                        candidate.toggleUnknown = br.toggleUnknown;
                        if (candidate.toggleUnknown ||
                            (candidate.unknownTrunc && context.hadToggleTargets)) {
                            context.sawToggleUnknown = true;
                        }
                        candidate.endX = br.endX;
                        candidate.deathX = br.deathX;
                        candidate.surviveTicks = ShadowSurvivalRules::rankingSurviveTicks(br);
                        candidate.surviveBin =
                            ShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
                        candidate.releaseAfter = offset;
                        candidate.requireHold = false;
                        context.finalizeViable(candidate, offset);
                        break;
                    }
                }
            }

            // Dash hold salvage (still two-pass gated to full-score set).
            if (candidate.raw.activatedRing && candidate.raw.wasDashing && !candidate.viable &&
                context.tickBudget > 0) {
                auto hold = context.runEngine(context.real, candidate.delay, 40,
                                              context.zone.valid ? &context.zone : nullptr);
                context.simulator.resetCaptureForNextRun(context.layer);
                SimulationCandidate height = candidate;
                height.raw = hold;
                height.activatedRing = hold.activatedRing;
                height.unknownTrunc = hold.unknownTrunc;
                height.toggleUnknown = hold.toggleUnknown;
                if (height.toggleUnknown || (height.unknownTrunc && context.hadToggleTargets)) {
                    context.sawToggleUnknown = true;
                }
                height.endX = hold.endX;
                height.deathX = hold.deathX;
                height.surviveTicks = ShadowSurvivalRules::rankingSurviveTicks(hold);
                height.surviveBin = ShadowSurvivalRules::surviveBinOf(height.surviveTicks);
                context.finalizeViable(height, 40);
                if (height.viable && !candidate.viable) {
                    candidate = height;
                    candidate.requireHold = true;
                    candidate.releaseAfter = 40;
                }
            }
        }

        if (sameCrashStopped) {
            AssistLog::debug(fmt::format("PLAN id={} prefixSim sameCrash={} prefixDeathTick={}",
                                         context.plan.planId, 1, prefixDeathTick));
        }
    }
}
