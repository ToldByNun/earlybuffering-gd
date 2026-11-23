#include "assist_release_planner.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../debug/assist_log.hpp"
#include "../../../../debug/lag_probe.hpp"
#include "../../../../debug/perf_trace.hpp"
#include "../../../../dev/capture/dev_clock_contract.hpp"
#include "../../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../../dev/capture/dev_trace_store.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../../ui/feedback/lag_warning_toast.hpp"
#include "../../../bake/bake_verifier.hpp"
#include "../../../diagnostics/assist_real_metrics.hpp"
#include "../../../diagnostics/divergence_tracker.hpp"
#include "../../../diagnostics/sim_tick_cost_meter.hpp"
#include "../../../learning/assist_learning.hpp"
#include "../../../learning/hybrid_learning.hpp"
#include "../../../learning/neural_learning.hpp"
#include "../../../learning/level_profile.hpp"
#include "../../../learning/spot_trust_map.hpp"
#include "../../../sim/planning/simulation_plan.hpp"
#include "../../../sim/shadow_simulator.hpp"
#include "../../../sim/sim_trust.hpp"
#include "../../../trigger_world/trigger_world_baker.hpp"
#include "../../../trigger_world/trigger_world_store.hpp"
#include "../../assist_controller.hpp"
#include "../../contracts/assist_verdict.hpp"
#include "../../contracts/live_plan_contracts.hpp"
#include "../../contracts/live_stage_parity.hpp"
#include "../../contracts/miss_assist_contracts.hpp"
#include "../../contracts/selected_tick_guard.hpp"
#include "../../flight/flight_assist_combine.hpp"
#include "../../input/click_intent_guard.hpp"
#include "../perfect_assist_policy.hpp"
#include "live_frame_budget_scope.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>

bool EbAssistReleasePlanner::planRelease(EbAssistController& controller, PlayLayer* playLayer,
                                         PlayerObject* player, bool player1, int effectTick,
                                         float firstDt, EbDevClockContract::Stamp const& clockIn) {
    EbDevClockContract::Stamp clock = clockIn;

    // Same rules as purple bake viz: wave/ship/swing/robot, else dash/holdAssist only.
    // Cube/ball/ufo/spider normal UP → Pass (no false release ASSIST).
    if (!controller.shouldPlanRelease(playLayer, player, player1, effectTick)) {
        EbAssistLog::debug(fmt::format("release: discrete-skip mode={} dashing={} holdAssistBake=0",
                                       EbStageConfig::kindName(EbStageConfig::kindOf(player)),
                                       player && player->m_isDashing ? 1 : 0));
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassReleaseDiscrete,
                                               "release-discrete-skip");
        controller.noteVerdict(AssistReason::PassReleaseDiscrete, "release-discrete-skip");
        controller.updateOverlay();
        return true;
    }

    int limit = controller.activeLimit(player);
    if (EbLiveStageParity::stageOffEquivalent(EbStageConfig::isEnabled(), limit)) {
        EbAssistRealMetrics::get().noteLiveDecision(EbLiveStageParity::DecisionKind::PassThrough,
                                                    false);
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassLimitZero, "release-limit0");
        controller.noteVerdict(AssistReason::PassLimitZero, "release-limit0");
        return true;
    }

    int const pressBudgetUs =
        EbLivePlanContracts::resolveLivePressBudgetUs(EbStageConfig::simBudgetUs());

    EbLiveFrameBudgetScope frameScope(controller.m_framePlanBudget);
    int const frameLimitUs = EbLiveFrameBudgetScope::frameLimit(pressBudgetUs);
    auto passFrameBudget = [&]() {
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassBudget, "frame-budget-exhausted");
        controller.noteVerdict(AssistReason::PassBudget, "frame-budget-exhausted");
        return true;
    };
    if (frameScope.remaining(frameLimitUs) <= 0)
        return passFrameBudget();

    {
        int const remUs = frameScope.remaining(frameLimitUs);
        int const shrunk = EbLiveStageParity::shrinkLimitUnderBudget(limit, remUs, frameLimitUs);
        if (shrunk < limit)
            limit = shrunk;
        if (limit <= 0)
            return passFrameBudget();
    }

    // GhostParity / SpotTrust: hard Pass before bake (same honesty as press).
    {
        float const xGate = player->getPositionX();
        AssistReason parityReason = AssistReason::None;
        if (EbSimTrust::get().forcePassReason(xGate, parityReason)) {
            if (EbHybridLearning::enabled())
                ++EbHybridLearning::get().drift;
            EbAssistLog::debug(
                fmt::format("SIMTRUST pass release ({})", EbAssistVerdict::name(parityReason)));
            controller.m_untrustPassHint = true;
            char const* where = parityReason == AssistReason::PassParityDrift
                                    ? "release-parity-drift"
                                    : "release-spot-untrusted";
            EbDevCycleRecorder::get().noteGatePass(parityReason, where);
            controller.noteVerdict(parityReason, where);
            controller.updateOverlay();
            return true;
        }
    }

    auto const kind = EbStageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = EbLevelProfile::get();
    int const slot = player1 ? 0 : 1;
    bool const nearOrb = EbAssistControllerImplementation::nearOrbOrPadHint(
        playLayer, player, limit, controller.m_simDt);

    if (EbDivergenceTracker::get().simUntrusted()) {
        if (!nearOrb) {
            EbAssistLog::debug("UNTRUST pass (release)");
            controller.m_untrustPassHint = true;
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassUntrust, "release-untrust");
            controller.noteVerdict(AssistReason::PassUntrust, "release-untrust");
            controller.updateOverlay();
            return true;
        }
        EbAssistLog::debug("UNTRUST soft (release orb foresight — allow plan)");
        controller.m_untrustPassHint = false;
    } else {
        controller.m_untrustPassHint = false;
    }

    // Commit 11/13: same PerfectAssist as press — trust+opportunity must Delay-search
    // (wave hold-longer saves must not lose to spam / PassNoThreat).
    EbPlanOptions learnedContext;
    int const learnedOpportunity =
        EbHybridLearning::get().prepare(playLayer, player, false, player1, effectTick, limit,
                                        controller.m_simDt, firstDt, learnedContext);
    int const aiOpportunity =
        EbNeuralLearning::get().prepare(playLayer, player, false, player1, effectTick, limit,
                                        controller.m_simDt, firstDt, learnedContext);
    controller.fillShipLineOptions(learnedContext, playLayer, player);
    controller.fillWavePatternOptions(learnedContext, playLayer, player);
    bool mustDelaySearch = learnedOpportunity > 0 || aiOpportunity > 0 ||
                           learnedContext.shipLine.enabled || learnedContext.wavePattern.enabled;
    {
        EbPerfectAssistPolicy::TrustInput trustIn;
        trustIn.parityDrift = EbSimTrust::get().parityDrift();
        trustIn.spotUntrusted = EbSpotTrustMap::get().isUntrusted(x);
        trustIn.divergenceUntrusted = EbDivergenceTracker::get().simUntrusted() && !nearOrb;
        if (EbStageConfig::triggerBake()) {
            auto& tw = EbTriggerWorldStore::get();
            auto& baker = EbTriggerWorldBaker::get();
            trustIn.twEmptyWhenExpected =
                tw.loaded() && !tw.hasTracks() && !baker.running() &&
                !baker.lastCollectIncomplete() &&
                (baker.lastBakedEffects() > 0 || baker.lastBakedStartTimed() > 0);
        }
        AssistReason namedPass = AssistReason::None;
        if (EbPerfectAssistPolicy::forcePassReason(trustIn, namedPass) &&
            namedPass == AssistReason::PassTwEmpty) {
            EbAssistLog::debug("PERFECT pass-tw-empty (release)");
            controller.m_untrustPassHint = true;
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassTwEmpty, "release-tw-empty");
            controller.noteVerdict(AssistReason::PassTwEmpty, "release-tw-empty");
            controller.updateOverlay();
            return true;
        }
        EbPerfectAssistPolicy::OpportunityInput oppIn;
        oppIn.foresightOrbPad = nearOrb;
        oppIn.bakePreferBand = false;
        oppIn.orbWindowFound = nearOrb;
        auto const decision = EbPerfectAssistPolicy::classify(trustIn, oppIn);
        mustDelaySearch =
            mustDelaySearch || decision == EbPerfectAssistPolicy::Decision::MustDelaySearch;
        if (mustDelaySearch) {
            EbAssistLog::debug("PERFECT must-delay-search release (trust+opportunity)");
        }
    }

    // Wave releases must reach the survival search even during fast turns.
    // Other continuous modes retain their existing rhythm filter.
    bool const continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
    if (continuous && !player->m_isDart && !mustDelaySearch) {
        if (EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind, nearOrb, false)) {
            EbAssistLog::debug(fmt::format("intent: spam Pass release (mode={}, tick={})",
                                           EbStageConfig::kindName(kind), effectTick));
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassSpam, "release-spam");
            controller.noteVerdict(AssistReason::PassSpam, "release-spam");
            controller.updateOverlay();
            return true;
        }
    }

    if (!mustDelaySearch && EbStageConfig::autoTune() && profile.loaded() && !nearOrb) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= EbLevelProfile::TRUST_PASS_MIN_N &&
                spot->trust < EbLevelProfile::TRUST_PASS_THRESHOLD &&
                !EbAssistLearning::shouldExploreDespiteLowTrust(spot->trust)) {
                EbAssistLog::debug(
                    fmt::format("learned: low trust @{} (release)", EbLevelProfile::spotBucket(x)));
                EbAssistLearning::decaySpot(kind, x);
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassLowTrust,
                                                       "release-low-trust");
                controller.noteVerdict(AssistReason::PassLowTrust, "release-low-trust");
                controller.updateOverlay();
                return true;
            }
        }
    }

    EbPlanOptions opts;
    opts.budgetUs = pressBudgetUs;
    opts.tickBudget = EbStageConfig::tickBudgetPerPress();
    opts.capMul = EbStageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    if (EbDivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;

    opts.attemptStartTick = effectTick;
    opts.legacyPick = EbStageConfig::assistPickLegacy();
    opts.assistEngine = EbStageConfig::effectiveAssistEngine();
    opts.mustDelaySearch = mustDelaySearch;
    // Releases use the same per-mode Adaptive settings as presses.
    opts.adaptivePlan = EbStageConfig::adaptivePlanFor(kind);
    opts.adaptiveScreenTicks = EbStageConfig::adaptiveScreenTicksFor(kind);
    opts.adaptiveVerifyQuantum = EbStageConfig::adaptiveVerifyQuantumFor(kind);
    opts.adaptiveReservePct = EbStageConfig::adaptiveReservePctFor(kind);

    // Live Delay-arm: only Alive ghosts (bake verify keeps soft Landing/LateCrash).
    opts.requireAliveForDelay = true;
    bool const spamDenseRel = EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
    EbAssistControllerImplementation::fillPlanRobustLatency(opts, player, controller.m_dtVarEma,
                                                            spamDenseRel);
    opts.hybridDelay = EbHybridLearning::get().prepare(
        playLayer, player, false, player1, effectTick, limit, controller.m_simDt, firstDt, opts);
    int const aiHint = EbNeuralLearning::get().prepare(
        playLayer, player, false, player1, effectTick, limit, controller.m_simDt, firstDt, opts);
    if (opts.hybridDelay > 0 || aiHint > 0)
        opts.mustDelaySearch = true;
    opts.shipLine = learnedContext.shipLine;
    opts.wavePattern = learnedContext.wavePattern;
    if (opts.shipLine.enabled || opts.wavePattern.enabled)
        opts.smartDepth = 1;
    int const remainingFrameUs = frameScope.remaining(frameLimitUs);
    if (remainingFrameUs <= 0)
        return passFrameBudget();
    opts.budgetUs = std::min(opts.budgetUs, remainingFrameUs);
    auto& phaseMeter = EbSimTickCostMeter::get();
    auto const worldBefore = phaseMeter.worldTotalUs();
    auto const updateBefore = phaseMeter.updateTotalUs();
    auto const collisionBefore = phaseMeter.collisionTotalUs();
    auto const callsBefore = phaseMeter.physicsTotalCount();
    auto const t0 = std::chrono::steady_clock::now();
    EbSimPlan plan{};
    bool scorerWon = false;
    auto armFromVerify = [&](int delayValue, bool aiPath, float confidence) {
        plan.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
        plan.fixedFireTick = true;
        if (delayValue <= 0) {
            plan.action = PlanAction::Pass;
            plan.reason = AssistReason::PassNoThreat;
            plan.nowIsSafe = true;
            plan.delay = 0;
        } else {
            plan.action = PlanAction::Delay;
            plan.reason = AssistReason::DelayThreat;
            plan.delay = plan.jumpTick = delayValue;
            plan.viableDelays = std::vector<int>{delayValue};
            plan.dMin = plan.dMax = delayValue;
            plan.guardOriginalDelay = delayValue;
            plan.threatCleared = true;
            plan.nowIsSafe = false;
        }
        if (aiPath) {
            plan.aiTried = true;
            plan.aiHint = delayValue;
            plan.aiConfidence = confidence;
            EbNeuralLearning::get().noteArmed(delayValue, confidence);
        } else {
            plan.hybridTried = true;
            plan.hybridHint = opts.hybridDelay;
        }
        scorerWon = true;
    };
    auto& neural = EbNeuralLearning::get();
    // Nova-aligned Live AI shell (see press planner) — not Autoplay cpuBudget cap.
    int const classicReserveUs = EbLivePlanContracts::novaAiClassicReserveUs(opts.budgetUs);
    int aiShellUs = EbLivePlanContracts::novaAiShellBudgetUs(opts.budgetUs);
    aiShellUs = std::max(aiShellUs, std::min(opts.budgetUs, EbNeuralLearning::minRemUsForAiFirst()));
    int aiSpendLeft = aiShellUs;
    if (EbNeuralLearning::active() && neural.preferAiFirst && !opts.shipLine.enabled &&
        !opts.wavePattern.enabled && !playLayer->m_gameState.m_isDualMode &&
        frameScope.remaining(frameLimitUs) >= EbNeuralLearning::minRemUsForAiFirst() &&
        aiSpendLeft >= EbNeuralLearning::minRemUsForAiFirst()) {
        auto const order = neural.rankedVerifyDelays(limit);
        int pickDelay = -1;
        float pickConf = 0.0f;
        int pickPass = -1;
        float pickPassConf = 0.0f;
        for (int delayValue : order) {
            if (delayValue < 0 || delayValue > limit)
                continue;
            int const remUs = frameScope.remaining(frameLimitUs);
            if (remUs < EbNeuralLearning::minRemUsForAiFirst() ||
                aiSpendLeft < EbNeuralLearning::minRemUsForAiFirst())
                break;
            EbPlanOptions vopts = opts;
            vopts.tickBudget = std::max(200, EbStageConfig::tickBudgetPerPress() / 3);
            vopts.budgetUs = std::min({opts.budgetUs, remUs, aiSpendLeft});
            vopts.maxBranches = 0;
            vopts.smartDepth = 1;
            auto const vr = EbShadowSimulator::get().verifyDelay(
                playLayer, player, false, delayValue, limit, controller.m_simDt, firstDt, vopts);
            int const spent = std::max(1, vr.usSpent);
            aiSpendLeft = std::max(0, aiSpendLeft - spent);
            opts.budgetUs = std::max(0, opts.budgetUs - spent);
            if (vr.budgetHit || vr.unknown)
                continue;
            if (!vr.ok) {
                neural.noteVerifyOutcome(delayValue, false);
                continue;
            }
            float const confidence = neural.confidenceForDelay(delayValue);
            neural.noteVerifyOutcome(delayValue, true);
            if (delayValue <= 0) {
                if (pickPass < 0) {
                    pickPass = 0;
                    pickPassConf = confidence;
                }
            } else if (pickDelay < 0) {
                pickDelay = delayValue;
                pickConf = confidence;
            }
        }
        if (pickDelay >= 0) {
            armFromVerify(pickDelay, true, pickConf);
            EbAssistLog::info(fmt::format("AI-FIRST release delay d={} ({:.0f}% sure)", pickDelay,
                                          pickConf * 100.0f));
        } else if (pickPass >= 0) {
            armFromVerify(0, true, pickPassConf);
            EbAssistLog::info(fmt::format("AI-FIRST release pass d=0 ({:.0f}% sure)",
                                          pickPassConf * 100.0f));
        }
        if (!scorerWon)
            opts.budgetUs = std::max(opts.budgetUs, classicReserveUs);
    }
    auto& hybrid = EbHybridLearning::get();
    // Hybrid-first stays Hybrid-only (AI may read tables for bootstrap, not this arm path).
    if (!scorerWon && EbHybridLearning::enabled() && hybrid.preferHybridFirst &&
        !opts.shipLine.enabled && !opts.wavePattern.enabled &&
        !playLayer->m_gameState.m_isDualMode) {
        auto const order = hybrid.rankedVerifyDelays(limit);
        for (int delayValue : order) {
            if (delayValue < 0 || delayValue > limit)
                continue;
            int const remUs = frameScope.remaining(frameLimitUs);
            if (remUs <= 0) {
                opts.budgetUs = 0;
                break;
            }
            EbPlanOptions vopts = opts;
            vopts.tickBudget = std::max(200, EbStageConfig::tickBudgetPerPress() / 3);
            vopts.budgetUs = std::min(opts.budgetUs, remUs);
            vopts.maxBranches = 0;
            vopts.smartDepth = 1;
            auto const vr = EbShadowSimulator::get().verifyDelay(
                playLayer, player, false, delayValue, limit, controller.m_simDt, firstDt, vopts);
            opts.budgetUs = std::max(0, opts.budgetUs - std::max(1, vr.usSpent));
            if (vr.budgetHit || vr.unknown)
                continue;
            if (!vr.ok) {
                hybrid.noteVerifyOutcome(delayValue, false);
                continue;
            }
            hybrid.noteVerifyOutcome(delayValue, true);
            armFromVerify(delayValue, false, 0.0f);
            EbAssistLog::info(fmt::format("HYBRID-FIRST release {} d={}",
                                          delayValue <= 0 ? "pass" : "delay", delayValue));
            break;
        }
    }
    if (!scorerWon) {
        // Nova: keep classic fallthrough even when the AI shell spent its slice.
        int const remAfterAi = frameScope.remaining(frameLimitUs);
        if (opts.budgetUs > 0)
            opts.budgetUs = std::min(opts.budgetUs, std::max(1, remAfterAi));
        else if (remAfterAi > 0)
            opts.budgetUs = remAfterAi;
        else
            opts.budgetUs = 1;
        plan = EbShadowSimulator::get().plan(playLayer, player, false, limit, controller.m_simDt,
                                             firstDt, opts);
    }
    plan.planUs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count());
    plan.hybridHint = opts.hybridDelay;
    plan.renderFrame = static_cast<long long>(frameScope.frame());
    plan.frameBudgetUs = frameLimitUs;
    plan.frameSpentBeforeUs = frameScope.spentBeforeUs();
    plan.plannerBudgetUs = opts.budgetUs;
    plan.physicsCalls = static_cast<int>(phaseMeter.physicsTotalCount() - callsBefore);
    plan.physicsWorldUs = static_cast<int>(phaseMeter.worldTotalUs() - worldBefore);
    plan.physicsUpdateUs = static_cast<int>(phaseMeter.updateTotalUs() - updateBefore);
    plan.physicsCollisionUs = static_cast<int>(phaseMeter.collisionTotalUs() - collisionBefore);
    EbLagWarningToast::get().notify(plan.planUs, opts.budgetUs, plan.budgetHit);
    clock.liveSimPathId = DevSimPathId::Plan;
    clock.effectTick = effectTick;
    EbDevCycleRecorder::get().notePlanFinished(plan, effectTick, limit, clock, playLayer, player);

    // Belt: never arm Delay when sim marked unknown / toggle-target uncertainty.
    if (plan.action == PlanAction::Delay && (plan.unknown || plan.toggleUnknown)) {
        EbAssistLog::debug(
            fmt::format("release: refuse Delay (toggle/unknown) → Pass id={}", plan.planId));
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.reason = AssistReason::PassToggleUnknown;
        plan.unknown = true;
        plan.toggleUnknown = true;
    }

    if (!plan.aiTried)
        plan.aiHint = aiHint;
    EbHybridLearning::get().decision(player1, plan);
    EbNeuralLearning::get().decision(player1, plan);
    if (plan.action != PlanAction::Delay) {
        AssistReason passReason = plan.reason;
        if (passReason == AssistReason::None) {
            passReason = AssistReason::PassUnknown;
        }
        EbAssistRealMetrics::get().noteLiveDecision(EbLiveStageParity::DecisionKind::AsMade, false);
        EbDevCycleRecorder::get().notePassThrough(passReason, "release-plan");
        controller.noteVerdict(passReason, "release-plan");
        controller.recordDecision(playLayer, player, false, plan, effectTick, 0, limit);
        return true;
    }

    int delay = plan.delay;
    int const plannedDelay = plan.delay;
    if (EbStageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            int off = profile.preferredDelayOffset(kind, x);
            bool const bakePrefer =
                opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin;
            bool const spamDense =
                EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
            if (EbAssistLearning::allowHistogramExplore(kind, spot->histN, spamDense, bakePrefer,
                                                        spot->diedAfterAssist)) {
                bool const freezeRand = EbDevConfig::recordEnable() &&
                                        EbDevConfig::disableRandom() &&
                                        EbDevCycleRecorder::get().isRecording();
                if (!freezeRand) {
                    off += (std::rand() % 3) - 1;
                }
                EbAssistLog::debug(fmt::format("learn: hist explore ±1 release @{} histN={}",
                                               EbLevelProfile::spotBucket(x), spot->histN));
            }
            if (spot->diedAfterAssist > 0 && off < 1) {
                off = 1;
            }
            delay = plannedDelay + off;
        }
        if (delay < plannedDelay) {
            delay = plannedDelay;
        }
        delay = EbMissAssistContracts::clampDelayToWindow(delay, limit);
        if (delay < 1)
            delay = 1;
    }

    // Learning may only move to a tick already verified by this plan.
    // AP09: flight/rhythm corrections keep planned on unverified / incomplete.
    bool const flightCorr = plan.shipLineCorrection || plan.wavePatternCorrection;
    delay = flightCorr ? EbFlightAssistCombine::guardFlightDelay(delay, plannedDelay,
                                                                plan.viableDelays, plan.budgetHit)
                       : EbSelectedTickGuard::keepVerifiedLearningDelay(delay, plannedDelay,
                                                                        plan.viableDelays);

    controller.m_buffer = {};
    controller.m_buffer.state = BufferState::Buffered;
    controller.m_buffer.down = false;
    controller.m_buffer.shipLineOwned = opts.shipLine.enabled && plan.shipLineCorrection;
    controller.m_buffer.wavePatternOwned =
        opts.wavePattern.enabled && plan.wavePatternCorrection;
    controller.m_buffer.fixedFireTick = plan.fixedFireTick;
    controller.m_buffer.planId = plan.planId;
    controller.m_buffer.player1 = player1;
    controller.m_buffer.pressTick = effectTick;
    controller.m_buffer.planDelay = delay;
    controller.m_buffer.limit = limit;
    // Releases have no jumpTick — fire at delay.
    controller.m_buffer.fireTick = controller.resolveFireTick(effectTick, delay, delay, false);
    controller.m_buffer.expectedJumpTick = controller.m_buffer.fireTick;
    controller.m_buffer.continuous = plan.continuous;
    controller.m_buffer.decisionX = x;
    controller.m_buffer.mode = kind;
    controller.m_buffer.fireTick = std::min(controller.m_buffer.fireTick, effectTick + limit);
    if (controller.m_buffer.shipLineOwned) {
        controller.m_lastShipLineDelay = delay;
        controller.m_lastShipLineTick = effectTick;
        EbAssistLog::info(fmt::format("SHIP-LINE release corr +{}", delay));
    }
    if (controller.m_buffer.wavePatternOwned) {
        controller.m_lastWavePatternDelay = delay;
        controller.m_lastWavePatternTick = effectTick;
        EbAssistLog::info(fmt::format("WAVE-PATTERN release corr +{}", delay));
    }

    EbAssistLog::debug(
        fmt::format("ASSIST release +{} ticks (now dies @+{}, limit {}, tick={} fire={})", delay,
                    plan.firstDeathTick, limit, effectTick, controller.m_buffer.fireTick));
    AssistReason delayReason = plan.reason;
    if (!EbAssistVerdict::isDelay(delayReason)) {
        delayReason = EbAssistVerdict::inferDelayReason(
            plan.activatedRing, plan.requireOrb,
            opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin);
    }
    {
        EbDevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = plan.budgetHit;
        controller.m_buffer.devPressId = EbDevCycleRecorder::get().noteDelayArm(
            delayReason, "release-delay", delay, controller.m_buffer.fireTick, plan.planId,
            DevEventSource::Live, meta);
    }
    controller.noteVerdict(delayReason, "release-delay");
    if (delay > 0)
        EbAssistRealMetrics::get().noteArm();
    {
        bool const legal = EbLiveStageParity::windowLegal(
            delay, limit, effectTick, controller.m_buffer.fireTick);
        EbAssistRealMetrics::get().noteLiveDecision(EbLiveStageParity::DecisionKind::Held, legal);
    }
    controller.recordDecision(playLayer, player, false, plan, effectTick, delay, limit);
    controller.updateOverlay();
    return false;
}
