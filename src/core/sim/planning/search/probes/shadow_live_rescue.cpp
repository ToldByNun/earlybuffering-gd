#include "shadow_live_rescue.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../assist/contracts/live_horizon_controller.hpp"
#include "../../../../assist/contracts/live_rescue_policy.hpp"
#include "../../../../assist/contracts/live_rescue_search.hpp"
#include "../idle_prefix_share.hpp"
#include "../shadow_plan_context.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <vector>

void EbShadowLiveRescue::evaluate(EbShadowPlanContext& context) {
    auto& plan = context.plan;
    plan.plannerPath = "liveRescue";
    plan.fixedFireTick = true;
    plan.action = PlanAction::Pass;
    plan.reason = AssistReason::PassHopeless;
    context.cands.resize(static_cast<std::size_t>(context.limit) + 1);
    context.candReady.assign(context.cands.size(), 0);
    // Every alternative starts from the same captured world, executes one input
    // edge, and runs to the same horizon or completed grounded action. No
    // hypothetical next click, release or shortened seed can prove a rescue.
    context.model.continuePastClear = true;
    context.model.stopAtCubeLanding =
        context.down && EbStageConfig::kindOf(context.real) == GameModeKind::Cube;
    // An actually activated orb can complete a Cube flight too, including
    // modeled gravity flips. A future orb opportunity is not an activation.
    context.model.allowOrbLanding = context.model.stopAtCubeLanding;
    // Nova runner 0x18031F680: 15 airborne ticks, then >4 ticks after
    // first contact at 240 TPS. Scale physical durations with the sim clock.
    float const tickScale = 1.f / (240.f * context.deltaTime);
    context.model.landingAirTicks = std::max(1, static_cast<int>(std::lround(15.f * tickScale)));
    context.model.landingContactTicks =
        std::max(2, static_cast<int>(std::lround(4.f * tickScale)) + 1);
    // Ball decisions end after a proved flip and stable landing. The next
    // obstacle requires a fresh user input and is a separate assist decision.
    context.model.stopAtBallLanding = context.down && context.real->m_isBall;
    context.model.probeOrbContinuation = false;
    context.model.probeSpiderContinuation = false;
    context.model.tapRelease = false;
    // Manual caps remain authoritative; automatic CPU adaptation never changes
    // the assist window and is frozen before evaluating d0.
    if (EbStageConfig::lookAheadCap() == 0 && EbStageConfig::paramsFor(context.real).cap == 0)
        context.model.cap =
            EbLiveHorizonController::scaledCap(context.model.cap, context.options.liveHorizonScale);
    int const horizon = context.limit + std::max(1, context.model.cap);
    plan.liveObservationTicks = horizon;
    EbLiveRescueSearch search(context.limit, true);
    EbIdlePrefixShare::Cursor prefix(context);
    bool const dual = context.linkedDual();
    auto classify = [&](EbShadowSimulator::RunResult const& result) {
        return EbLiveRescuePolicy::classify(
            result.died, result.unknown || result.unknownTrunc || result.toggleUnknown,
            result.diedBeforeAction, result.ticksRan, horizon,
            (context.model.stopAtBallLanding || context.model.stopAtCubeLanding) &&
                result.landedAfterAction);
    };
    auto probe = [&](int delay, bool sharePrefix = true) {
        EbSimulationCandidate candidate;
        bool const shared = sharePrefix && prefix.probe(delay);
        if (shared) {
            candidate = context.cands[static_cast<std::size_t>(delay)];
        } else {
            prefix.invalidate();
            candidate = context.evalDelay(delay, -1);
        }
        if (shared && EbStageConfig::assistVerifyAb()) {
            // Existing opt-in A/B gate checks real GD state/physics equivalence.
            // The independent result remains authoritative for that diagnostic run.
            prefix.invalidate();
            auto independent = context.evalDelay(delay, -1);
            if (classify(candidate.raw) != classify(independent.raw) ||
                candidate.raw.deathTick != independent.raw.deathTick ||
                std::abs(candidate.raw.endX - independent.raw.endX) > 0.01f)
                geode::log::warn("[EarlyBuffer] live prefix A/B mismatch at delay {}", delay);
            candidate = independent;
        }
        auto state = classify(candidate.raw);
        if (dual && state == EbLiveRescuePolicy::Observation::Safe) {
            auto* other = context.real == context.layer->m_player1 ? context.layer->m_player2
                                                                   : context.layer->m_player1;
            auto partner = context.runRestoredProbe(other, delay);
            state = classify(partner);
            if (delay == 0 && state != EbLiveRescuePolicy::Observation::Safe)
                candidate.raw = partner;
        }
        candidate.viable = state == EbLiveRescuePolicy::Observation::Safe;
        candidate.clearsThreat = candidate.viable;
        auto const index = static_cast<std::size_t>(delay);
        context.cands[index] = candidate;
        context.candReady[index] = candidate.raw.ticksRan > 0;
        return state;
    };
    auto finish = [&]() {
        prefix.invalidate();
        if (EbStageConfig::debugLogging())
            EbAssistLog::debug(
                fmt::format("LIVE-RESCUE id={} horizon={} scale={:.2f} candidates={} idleSaved={}",
                            plan.planId, horizon, context.options.liveHorizonScale,
                            plan.candidatesTried, plan.sharedIdleTicksSaved));
        // This path already proved the exact chosen edge and input model. Do
        // not reinterpret it with the legacy short/landing proof contract.
        plan.guardOriginalDelay = plan.delay;
        context.publishLedger(plan.delay, plan.action == PlanAction::Pass,
                              context.ledgerEarlyExit());
    };
    bool incompleteSeen = false;
    int const initialTickBudget = context.tickBudget;
    for (int delay = search.nextDelay(); delay >= 0; delay = search.nextDelay()) {
        if (context.tickBudget <= 0 || context.wallExceeded()) {
            plan.budgetHit = true;
            plan.reason = AssistReason::PassBudget;
            break;
        }
        if (context.tickBudget < initialTickBudget / 2 ||
            (context.options.budgetUs > 0 && context.wallRemUs() < context.options.budgetUs / 2))
            search.reserveFallback();
        auto state = probe(delay);
        if (delay == 0) {
            search.record(0, state);
            auto const& baseline = context.cands[0].raw;
            plan.firstDeathTick = baseline.deathTick;
            plan.firstDeathObject = baseline.deathObject;
            if (state == EbLiveRescuePolicy::Observation::Safe) {
                plan.reason = AssistReason::PassD0Safe;
                plan.nowIsSafe = true;
                plan.threatCleared = true;
                finish();
                return;
            }
            if (state == EbLiveRescuePolicy::Observation::Incomplete) {
                plan.budgetHit = context.tickBudget <= 0 || context.wallExceeded();
                plan.unknown = !plan.budgetHit;
                plan.toggleUnknown = baseline.toggleUnknown;
                plan.reason = plan.budgetHit ? AssistReason::PassBudget : AssistReason::PassUnknown;
                break;
            }
            continue;
        }
        if (state != EbLiveRescuePolicy::Observation::Safe) {
            incompleteSeen = incompleteSeen || state == EbLiveRescuePolicy::Observation::Incomplete;
            search.record(delay, state);
            continue;
        }
        if (context.options.robustJitter) {
            float const savedFirstDt = context.firstDt;
            float const variation = std::clamp(context.options.dtVariance, 0.03f, 0.1f);
            for (float scale : {1.f - variation, 1.f + variation}) {
                context.firstDt = savedFirstDt * scale;
                ++plan.guardProbeCount;
                state = probe(delay, false);
                if (state != EbLiveRescuePolicy::Observation::Safe)
                    break;
            }
            context.firstDt = savedFirstDt;
            if (state != EbLiveRescuePolicy::Observation::Safe) {
                plan.guardProbeIncomplete = state == EbLiveRescuePolicy::Observation::Incomplete;
                incompleteSeen =
                    incompleteSeen || state == EbLiveRescuePolicy::Observation::Incomplete;
                search.record(delay, state);
                continue;
            }
        }
        search.record(delay, state);
    }
    int const selected = search.selectedDelay();
    if (selected > 0) {
        auto const& winner = context.cands[static_cast<std::size_t>(selected)];
        plan.action = PlanAction::Delay;
        plan.reason = AssistReason::DelayThreat;
        plan.delay = plan.jumpTick = selected;
        plan.activatedRing = winner.activatedRing;
        plan.threatCleared = true;
        plan.viableDelays = search.safeBand();
        plan.dMin = plan.viableDelays.front();
        plan.dMax = plan.viableDelays.back();
    } else if (incompleteSeen && plan.reason == AssistReason::PassHopeless) {
        plan.reason = AssistReason::PassUnknown;
        plan.unknown = true;
    }
    finish();
}
