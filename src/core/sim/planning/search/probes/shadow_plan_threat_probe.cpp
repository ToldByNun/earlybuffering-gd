#include "shadow_plan_threat_probe.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/planning/opportunity_probe.hpp"
#include "../../../../assist/planning/perfect_assist_policy.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../simulation_plan.hpp"
#include "../shadow_plan_context.hpp"
#include "shadow_plan_foresight.hpp"
#include <algorithm>

bool ShadowPlanThreatProbe::evaluate(ShadowPlanContext& context) {
    {
        auto c0 = context.evalDelay(0, -1);
        context.plan.firstDeathTick = c0.raw.died ? c0.raw.deathTick : -1;
        context.plan.firstDeathObject = c0.raw.deathObject;
        if (c0.raw.died) {
            // Hybrid verify already clustered this zone — reuse (skip rebuild).
            if (context.options.seedThreatZone.valid) {
                context.zone = context.options.seedThreatZone;
            } else {
                context.zone = context.simulator.buildThreatZone(context.layer, c0.raw.deathX,
                                                                 c0.raw.deathObject);
            }
            context.plan.threatZone = context.zone;
        }
        context.finalizeViable(c0, -1);
        context.cands[0] = c0;
        context.candReady[0] = 1;

        // Continuous: idle (baseline coast) safer than d0 action → miss-assist may Delay.
        // For release, baseline = keep holding, d0 = UP now — same compare (enable always).
        context.plan.idleSafer = context.simulator.classifyIdleSafer(
            context.model.continuous, true, context.plan.baselineDeathTick >= 0,
            context.plan.baselineDeathTick, context.plan.baselineDeathX, c0.raw.died,
            c0.raw.died ? c0.raw.deathTick : -1, c0.raw.died ? c0.raw.deathX : c0.endX);

        // Live V2: LongForesight press-vs-idle over AssistHorizon (extend if press dies).
        ShadowPlanForesight::probe(context);

        // No threat target: baseline survives and no orb/pad ahead.
        // G2: OpportunityProbe (foresight / mid+limit) may still force full search.
        bool const noThreat = !c0.raw.died && !context.zone.valid;
        bool const noOrbTarget = !(context.orbAssist && (context.orbHint.found ||
                                                         context.spatialHint || c0.activatedRing));
        if (noThreat && noOrbTarget) {
            OpportunityProbe::Sample baseSample{};
            baseSample.activatedRing = c0.activatedRing;
            baseSample.diedBeforeAction = c0.diedBeforeAction;
            baseSample.surviveBin = c0.surviveBin;
            baseSample.endX = c0.endX;

            OpportunityProbe::Sample probeSamples[2];
            int probeCount = 0;
            int const mid = OpportunityProbe::midDelay(context.limit);
            auto runProbeDelay = [&](int delay) {
                if (delay <= 0 || delay > context.limit)
                    return;
                if (context.candReady[static_cast<size_t>(delay)])
                    return;
                if (context.tickBudget < OpportunityProbe::kMinBudgetForProbe ||
                    context.wallExceeded()) {
                    context.plan.budgetHit = true;
                    return;
                }
                auto candidate = context.evalDelay(delay, -1);
                if (candidate.diedBeforeAction) {
                    // Still record — probe may show nothing useful.
                    context.finalizeViable(candidate, -1);
                    context.cands[static_cast<size_t>(delay)] = candidate;
                    context.candReady[static_cast<size_t>(delay)] = 1;
                    if (probeCount < 2) {
                        auto& ps = probeSamples[probeCount++];
                        ps.activatedRing = candidate.activatedRing;
                        ps.diedBeforeAction = candidate.diedBeforeAction;
                        ps.surviveBin = candidate.surviveBin;
                        ps.endX = candidate.endX;
                    }
                    return;
                }
                context.finalizeViable(candidate,
                                       candidate.releaseAfter >= 0 ? candidate.releaseAfter : -1);
                context.cands[static_cast<size_t>(delay)] = candidate;
                context.candReady[static_cast<size_t>(delay)] = 1;
                if (probeCount < 2) {
                    auto& ps = probeSamples[probeCount++];
                    ps.activatedRing = candidate.activatedRing;
                    ps.diedBeforeAction = candidate.diedBeforeAction;
                    ps.surviveBin = candidate.surviveBin;
                    ps.endX = candidate.endX;
                }
            };

            if (context.limit >= 1) {
                runProbeDelay(mid);
                if (context.limit != mid) {
                    runProbeDelay(context.limit);
                }
            }

            bool searchFurther = OpportunityProbe::shouldSearchFurther(
                context.spatialHint, context.orbHint.found, c0.activatedRing, baseSample,
                probeSamples, probeCount);
            // Commit 11: trusted opportunity → MUST Delay-search (skip no-threat exit).
            if (context.options.mustDelaySearch)
                searchFurther = true;
            // Live V2 LongForesight: press dies on long horizon → keep searching.
            if (context.plan.longForesightNeedSearch)
                searchFurther = true;

            if (!searchFurther) {
                context.plan.action = PlanAction::Pass;
                // Wanted probes but could not afford any → PassBudget (C6 may Hold).
                // Commit 11: PassBudget only when probe budget is exhausted (allowPassBudget).
                if (context.plan.budgetHit && context.limit >= 1 && probeCount == 0 &&
                    PerfectAssistPolicy::allowPassBudget(true, context.options.mustDelaySearch,
                                                         false)) {
                    context.plan.reason = AssistReason::PassBudget;
                } else {
                    context.plan.reason = AssistReason::PassNoThreat;
                }
                context.plan.delay = 0;
                context.plan.nowIsSafe = true;
                context.plan.threatCleared = true;
                context.plan.jumpTick = c0.jumpTick;
                context.plan.activatedRing = c0.activatedRing;
                AssistLog::debug(fmt::format(
                    "PLAN id={} Pass no-threat verdict={} foresight={} probes={} "
                    "budgetHit={} mustSearch={} limit={}",
                    context.plan.planId, AssistVerdict::name(context.plan.reason),
                    context.plan.foresightOrbPad ? 1 : 0, probeCount, context.plan.budgetHit,
                    context.options.mustDelaySearch ? 1 : 0, context.limit));
                context.publishLedger(0, true,
                                      context.plan.budgetHit
                                          ? DevRejectLedger::EarlyExit::Budget
                                          : DevRejectLedger::EarlyExit::NoThreat);
                return true;
            }
            AssistLog::debug(fmt::format(
                "PLAN id={} opportunity-probe search-further foresight={} probes={} orbWin={}",
                context.plan.planId, context.plan.foresightOrbPad ? 1 : 0, probeCount,
                context.orbHint.found ? 1 : 0));
        }

        // Wave zigzag (press only): hazards in proximity ahead + no orb → Pass
        // even if d0 built a distant ThreatZone. Delaying phase-shifts into spikes.
        // Release Delay = hold longer — never Pass that path near hazards.
        // AFTER foresight: foresightOrbPad blocks this Pass (miss-assist orb).
        bool const isWave = context.real && context.real->m_isDart;
        float const proxX = std::clamp(static_cast<float>(context.limit + 4) * 5.5f,
                                       ShadowSimulator::kHazardProximityXMin,
                                       ShadowSimulator::kHazardProximityXMax);
        bool const nearHazard =
            isWave && context.simulator.spatialHazardProximity(context.layer, context.real, proxX);
        // Only suppress when d0 died (need to search for a living save).
        // idleSafer alone must not skip this Pass — Alive later is checked later.
        if (!c0.raw.died &&
            context.simulator.shouldWaveHazardProximityPass(
                isWave, nearHazard, noOrbTarget, context.plan.foresightOrbPad, context.down)) {
            context.plan.action = PlanAction::Pass;
            context.plan.reason = AssistReason::PassWaveProximity;
            context.plan.delay = 0;
            context.plan.nowIsSafe = c0.viable;
            context.plan.threatCleared = c0.clearsThreat || !context.zone.valid;
            context.plan.jumpTick = c0.jumpTick;
            context.plan.activatedRing = c0.activatedRing;
            context.plan.threatZone = context.zone;
            AssistLog::debug(fmt::format(
                "PLAN id={} Pass wave-hazard-proximity verdict={} prox={:.0f} zone={} budgetHit={}",
                context.plan.planId, AssistVerdict::name(context.plan.reason), proxX,
                context.zone.valid, context.plan.budgetHit));
            context.publishLedger(0, true, DevRejectLedger::EarlyExit::WaveProximity);
            return true;
        }
    }

    return false;
}
