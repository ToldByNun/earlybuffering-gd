#include "assist_press_planner.hpp"
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
#include "../../../diagnostics/assist_perf_stats.hpp"
#include "../../../diagnostics/assist_real_metrics.hpp"
#include "../../../diagnostics/divergence_tracker.hpp"
#include "../../../diagnostics/sim_tick_cost_meter.hpp"
#include "../../../learning/assist_learning.hpp"
#include "../../../learning/hybrid_learning.hpp"
#include "../../../learning/ai_config.hpp"
#include "../../../learning/neural_learning.hpp"
#include "../../../learning/level_profile.hpp"
#include "../../../real_assist/real_assist_runner.hpp"
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
#include "../orb_align.hpp"
#include "../perfect_assist_policy.hpp"
#include "live_frame_budget_scope.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>

bool EbAssistPressPlanner::planPress(EbAssistController& controller, PlayLayer* playLayer,
                                     PlayerObject* player, bool player1, int effectTick,
                                     float firstDt, EbDevClockContract::Stamp const& clockIn) {
    EbDevClockContract::Stamp clock = clockIn;
    int limit = controller.activeLimit(player);
    bool const stageOff =
        EbLiveStageParity::stageOffEquivalent(EbStageConfig::isEnabled(), limit);
    if (stageOff) {
        // Nova Stage-Off equivalent — raw input, no buffering.
        EbAssistRealMetrics::get().noteLiveDecision(EbLiveStageParity::DecisionKind::PassThrough,
                                                    false);
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassLimitZero, "press-limit0");
        controller.noteVerdict(AssistReason::PassLimitZero, "press-limit0");
        return true;
    }

    // Opportunity (Commit 2): every press that reaches assist decisioning.
    {
        float const xOpp = player->getPositionX();
        bool const trusted = EbSimTrust::get().okAt(xOpp);
        EbAssistRealMetrics::get().noteOpportunity(trusted);
    }

    // Nova InputBuffer: soft wall clamps hitch; when short on time it shrinks lookahead
    // and still buffers — never starves classic fallthrough for an AI shell alone.
    int const pressBudgetUs =
        EbLivePlanContracts::resolveLivePressBudgetUs(EbStageConfig::simBudgetUs());
    int aiCpuCapUs = 0;
    if (EbNeuralLearning::active()) {
        auto const& ai = EbAiConfig::get();
        aiCpuCapUs = std::min({ai.maxAiCpuUsPerTick, ai.cpuBudgetUsPerTick, ai.searchTimeoutUs});
    }

    EbLiveFrameBudgetScope frameScope(controller.m_framePlanBudget);
    int const frameLimitUs = EbLiveFrameBudgetScope::frameLimit(pressBudgetUs);
    auto passFrameBudget = [&]() {
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassBudget, "frame-budget-exhausted");
        controller.noteVerdict(AssistReason::PassBudget, "frame-budget-exhausted");
        return true;
    };
    if (frameScope.remaining(frameLimitUs) <= 0)
        return passFrameBudget();

    // AP-NL2: Nova-style lookahead shrink when the shared *frame* wall is half-spent.
    // Must use frameLimitUs (not hitch ceiling) — research_20261122_191833: Stage-12
    // shrunk to d≤4 because rem~1.5ms was compared against fullBudget=8000.
    {
        int const remUs = frameScope.remaining(frameLimitUs);
        int const shrunk = EbLiveStageParity::shrinkLimitUnderBudget(limit, remUs, frameLimitUs);
        if (shrunk < limit)
            limit = shrunk;
        if (limit <= 0)
            return passFrameBudget();
    }

    // GhostParity / SpotTrust: hard Pass before bake+Live — never soft with orb.
    // Commit 11: reasons via PerfectAssistPolicy / SimTrust (same tokens).
    {
        float const xGate = player->getPositionX();
        AssistReason parityReason = AssistReason::None;
        if (EbSimTrust::get().forcePassReason(xGate, parityReason)) {
            if (EbHybridLearning::enabled())
                ++EbHybridLearning::get().drift;
            EbAssistLog::debug(
                fmt::format("SIMTRUST pass press ({})", EbAssistVerdict::name(parityReason)));
            controller.m_untrustPassHint = true;
            EbSimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = parityReason;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            char const* where = parityReason == AssistReason::PassParityDrift
                                    ? "press-parity-drift"
                                    : "press-spot-untrusted";
            EbDevCycleRecorder::get().noteGatePass(parityReason, where);
            controller.noteVerdict(parityReason, where);
            controller.recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
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

    // Live/Hybrid-live: sim mismatch → hard Pass, unless orb/pad ahead
    // (allow Delay / Soft Hold — solitary early orb miss must still assist).
    if (EbDivergenceTracker::get().simUntrusted()) {
        if (!nearOrb) {
            EbAssistLog::debug("UNTRUST pass (press)");
            controller.m_untrustPassHint = true;
            EbSimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = AssistReason::PassUntrust;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassUntrust, "press-untrust");
            controller.noteVerdict(AssistReason::PassUntrust, "press-untrust");
            controller.recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
            controller.updateOverlay();
            return true;
        }
        EbAssistLog::debug("UNTRUST soft (press orb foresight — allow plan)");
        controller.m_untrustPassHint = false;
    } else {
        controller.m_untrustPassHint = false;
    }

    // Commit 11: Opportunity + Trust → MUST Delay-search (AssistCov).
    // Named Pass for TW-empty-when-expected (effects baked, 0 tracks).
    EbPlanOptions learnedContext;
    int const learnedOpportunity =
        EbHybridLearning::get().prepare(playLayer, player, true, player1, effectTick, limit,
                                        controller.m_simDt, firstDt, learnedContext);
    int const aiOpportunity =
        EbNeuralLearning::get().prepare(playLayer, player, true, player1, effectTick, limit,
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
            EbAssistLog::debug("PERFECT pass-tw-empty (expected movers, 0 tracks)");
            controller.m_untrustPassHint = true;
            EbSimPlan dummy;
            dummy.action = PlanAction::Pass;
            dummy.reason = AssistReason::PassTwEmpty;
            dummy.firstDeathTick = -1;
            dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            EbDevCycleRecorder::get().noteGatePass(AssistReason::PassTwEmpty, "press-tw-empty");
            controller.noteVerdict(AssistReason::PassTwEmpty, "press-tw-empty");
            controller.recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
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
            EbAssistLog::debug("PERFECT must-delay-search (trust+opportunity)");
        }
    }

    // Dense / rhythmic spam (esp. wave corridors): Pass unless orb/pad or bake nearby.
    // Trusted opportunity must still Delay-search — never spam-Pass over AssistCov.
    if (!player->m_isBird && !mustDelaySearch &&
        EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind, nearOrb, false)) {
        EbAssistLog::debug(fmt::format("intent: spam Pass (mode={}, tick={})",
                                       EbStageConfig::kindName(kind), effectTick));
        EbSimPlan dummy;
        dummy.action = PlanAction::Pass;
        dummy.reason = AssistReason::PassSpam;
        dummy.firstDeathTick = -1;
        dummy.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
        EbDevCycleRecorder::get().noteGatePass(AssistReason::PassSpam, "press-spam");
        controller.noteVerdict(AssistReason::PassSpam, "press-spam");
        controller.recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
        controller.updateOverlay();
        return true;
    }

    // Low trust at this spot → pass through (unless explore roll or orb ahead).
    // Commit 11: trusted opportunity MustDelaySearch overrides learned low-trust Pass.
    if (!mustDelaySearch && EbStageConfig::autoTune() && profile.loaded() && !nearOrb) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= EbLevelProfile::TRUST_PASS_MIN_N &&
                spot->trust < EbLevelProfile::TRUST_PASS_THRESHOLD &&
                !EbAssistLearning::shouldExploreDespiteLowTrust(spot->trust)) {
                EbAssistLog::debug(fmt::format("learned: low trust @{} (trust {:.2f}, n={})",
                                               EbLevelProfile::spotBucket(x), spot->trust,
                                               spot->n));
                EbAssistLearning::decaySpot(kind, x);
                EbSimPlan dummy;
                dummy.action = PlanAction::Pass;
                dummy.reason = AssistReason::PassLowTrust;
                dummy.firstDeathTick = -1;
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassLowTrust,
                                                       "press-low-trust");
                controller.noteVerdict(AssistReason::PassLowTrust, "press-low-trust");
                controller.recordDecision(playLayer, player, true, dummy, effectTick, 0, limit);
                controller.updateOverlay();
                return true;
            }
        }
    }

    EbPlanOptions opts;
    // Hybrid fallthrough: remainder after bake verify (not a second full envelope).
    // Exhausted remainder (usLeft==0) must NOT map to PlanOptions unlimited.
    // AP1 + AI: never hand Shadow budgetUs<=0 (unlimited) — research_20261122_085548
    // showed 7–20ms classicRequired spikes with sim-budget-us=0 in AI mode.
    opts.budgetUs = pressBudgetUs;
    opts.tickBudget = EbStageConfig::tickBudgetPerPress();
    opts.capMul = EbStageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    // Soft warn: shorten look-ahead slightly (no hard Pass).
    if (EbDivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;
    opts.smartDepth = EbStageConfig::smartDepth();
    opts.smartCombos = EbStageConfig::smartCombos();
    opts.smartHorizon = EbStageConfig::smartHorizon();
    // Adaptive Prediction Planner — kind-resolved (Modes Adaptive Overwrite or Main).
    opts.adaptivePlan = EbStageConfig::adaptivePlanFor(kind);
    opts.adaptiveScreenTicks = EbStageConfig::adaptiveScreenTicksFor(kind);
    opts.adaptiveVerifyQuantum = EbStageConfig::adaptiveVerifyQuantumFor(kind);
    opts.adaptiveReservePct = EbStageConfig::adaptiveReservePctFor(kind);

    opts.attemptStartTick = effectTick;
    opts.legacyPick = EbStageConfig::assistPickLegacy();
    opts.assistEngine = EbStageConfig::effectiveAssistEngine();
    opts.mustDelaySearch = mustDelaySearch;
    // Live Delay-arm: only Alive ghosts (bake verify keeps soft Landing/LateCrash).
    opts.requireAliveForDelay = true;
    opts.allowSubtick =
        clock.midStep && player1 && player->m_isBird && !playLayer->m_gameState.m_isDualMode;

    // Density without orb/bake override — softens Wave λ / bumps auto k when
    // we still assist (e.g. orb in spam corridor).
    bool const spamDense = EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
    EbAssistControllerImplementation::fillPlanRobustLatency(opts, player, controller.m_dtVarEma,
                                                            spamDense);
    opts.hybridDelay = EbHybridLearning::get().prepare(playLayer, player, true, player1, effectTick,
                                                       limit, controller.m_simDt, firstDt, opts);
    int const aiHint =
        EbNeuralLearning::get().prepare(playLayer, player, true, player1, effectTick, limit,
                                        controller.m_simDt, firstDt, opts);
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
    // planSequence only when Smart Depth > 1 (New depth-1 exit-predict removed).
    if (!opts.shipLine.enabled && !opts.wavePattern.enabled && opts.hybridDelay >= 0)
        opts.smartDepth = std::max(opts.smartDepth, EbHybridLearning::get().lineupDepth);
    bool useSequence = opts.smartDepth > 1;

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
    // AI-first: score → top-k → verifyDelay → arm (never argmax-arm). Hybrid stays Hybrid-only.
    // AI shell = up to half the press wall (and AI CPU cap) — Adaptive's discrete half+
    // reserve left Shell=1 on ~3ms frames so AI never ran (research_20261122_194245).
    // Prefer verified delay>0 over d=0 so cold ranks do not starve Delays.
    auto& neural = EbNeuralLearning::get();
    int const classicReserveUs =
        std::min(opts.budgetUs / 2, EbLivePlanContracts::kFallthroughRemUsFloor);
    int aiShellUs =
        EbLivePlanContracts::adaptiveShellBudgetUs(opts.budgetUs, classicReserveUs);
    if (aiCpuCapUs > 0)
        aiShellUs = std::min(aiShellUs, std::max(800, aiCpuCapUs));
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
            // Enough ticks to actually prove Alive — /8 was starving Delay verifies.
            vopts.tickBudget = std::max(200, EbStageConfig::tickBudgetPerPress() / 3);
            vopts.budgetUs = std::min({opts.budgetUs, remUs, aiSpendLeft});
            vopts.maxBranches = 0;
            vopts.smartDepth = 1;
            auto const vr = EbShadowSimulator::get().verifyDelay(
                playLayer, player, true, delayValue, limit, controller.m_simDt, firstDt, vopts);
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
            EbAssistLog::info(fmt::format("AI-FIRST delay d={} ({:.0f}% sure)", pickDelay,
                                          pickConf * 100.0f));
        } else if (pickPass >= 0) {
            armFromVerify(0, true, pickPassConf);
            EbAssistLog::info(
                fmt::format("AI-FIRST pass d=0 ({:.0f}% sure)", pickPassConf * 100.0f));
        }
        // Keep classic fallthrough solvent after AI verifies — do not gift away the reserve.
        if (!scorerWon)
            opts.budgetUs = std::max(opts.budgetUs, classicReserveUs);
    }
    // Hybrid-first: tabular UCB order → verify (does not run under AI mode; enabled() gates).
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
                playLayer, player, true, delayValue, limit, controller.m_simDt, firstDt, vopts);
            opts.budgetUs = std::max(0, opts.budgetUs - std::max(1, vr.usSpent));
            if (vr.budgetHit || vr.unknown)
                continue;
            if (!vr.ok) {
                hybrid.noteVerifyOutcome(delayValue, false);
                continue;
            }
            hybrid.noteVerifyOutcome(delayValue, true);
            armFromVerify(delayValue, false, 0.0f);
            EbAssistLog::info(fmt::format("HYBRID-FIRST {} d={}",
                                          delayValue <= 0 ? "pass" : "delay", delayValue));
            break;
        }
    }
    if (!scorerWon) {
        // Nova: out-of-time still plans with shrunk lookahead — classic fallthrough always runs.
        int const remAfterAi = frameScope.remaining(frameLimitUs);
        if (opts.budgetUs > 0)
            opts.budgetUs = std::min(opts.budgetUs, std::max(1, remAfterAi));
        else if (remAfterAi > 0)
            opts.budgetUs = remAfterAi;
        else
            opts.budgetUs = 1; // tiny classic slice beats inventing PassBudget
        plan = useSequence ? EbShadowSimulator::get().planSequence(
                                 playLayer, player, true, limit, controller.m_simDt, firstDt, opts)
                           : EbShadowSimulator::get().plan(playLayer, player, true, limit,
                                                           controller.m_simDt, firstDt, opts);
    }
    int const planUs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count());
    plan.planUs = planUs;
    plan.hybridHint = opts.hybridDelay;
    plan.renderFrame = static_cast<long long>(frameScope.frame());
    plan.frameBudgetUs = frameLimitUs;
    plan.frameSpentBeforeUs = frameScope.spentBeforeUs();
    plan.plannerBudgetUs = opts.budgetUs;
    plan.physicsCalls = static_cast<int>(phaseMeter.physicsTotalCount() - callsBefore);
    plan.physicsWorldUs = static_cast<int>(phaseMeter.worldTotalUs() - worldBefore);
    plan.physicsUpdateUs = static_cast<int>(phaseMeter.updateTotalUs() - updateBefore);
    plan.physicsCollisionUs = static_cast<int>(phaseMeter.collisionTotalUs() - collisionBefore);
    EbLagWarningToast::get().notify(planUs, opts.budgetUs, plan.budgetHit);
    EbAssistPerfStats::get().notePlan(planUs, plan.budgetHit);
    // Classic coverage proxy until Adaptive reports real fully-verified counts.
    int const fullyVerified = plan.budgetHit ? 0 : std::max(0, plan.candidatesTried);
    EbSimTickCostMeter::get().notePlanOutcome(plan.budgetHit, plan.candidatesTried, fullyVerified);

    controller.logPlan(plan, "press");
    controller.m_outcomePlayer1 = player1;
    controller.beginOutcome(plan, effectTick);
    clock.liveSimPathId = useSequence ? DevSimPathId::PlanSequence : DevSimPathId::Plan;
    clock.effectTick = effectTick;
    EbDevCycleRecorder::get().notePlanFinished(plan, effectTick, limit, clock, playLayer, player);

    // Belt: never arm Delay when sim marked unknown / toggle-target uncertainty.
    if (plan.action == PlanAction::Delay && (plan.unknown || plan.toggleUnknown)) {
        EbAssistLog::debug(
            fmt::format("press: refuse Delay (toggle/unknown) → Pass id={}", plan.planId));
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.reason = AssistReason::PassToggleUnknown;
        plan.unknown = true;
        plan.toggleUnknown = true;
    }

    // Commit 11: do not invent Delay without a viable cand (plan already gated).
    // budgetHit after a full pick is OK — Soft Hold covers PassBudget separately.

    if (!plan.aiTried)
        plan.aiHint = aiHint;
    EbHybridLearning::get().decision(player1, plan);
    EbNeuralLearning::get().decision(player1, plan);
    if (plan.action != PlanAction::Delay) {
        AssistReason passReason = plan.reason;
        if (passReason == AssistReason::None) {
            passReason = AssistReason::PassUnknown;
        }
        // AP-NL2: Pass with buffering on ≡ Nova "in as made" (click as made).
        EbAssistRealMetrics::get().noteLiveDecision(EbLiveStageParity::DecisionKind::AsMade, false);
        EbAssistLog::debug(fmt::format("live-decision {} reason={}",
                                       EbLiveStageParity::decisionLabel(
                                           EbLiveStageParity::DecisionKind::AsMade),
                                       EbAssistVerdict::name(passReason)));
        EbDevCycleRecorder::get().notePassThrough(passReason, "press-plan");
        controller.noteVerdict(passReason, "press-plan");
        controller.recordDecision(playLayer, player, true, plan, effectTick, 0, limit);
        // Vanilla/Pass click timing — feed earliness histogram (never inject saves).
        if (EbStageConfig::autoTune() && profile.loaded()) {
            profile.noteEarliness(kind, x, 0, "pass");
        }
        return true;
    }

    int delay = plan.delay;
    int const plannedDelay = plan.delay;
    bool learningFloored = false;
    bool learningNudged = false;
    if (EbStageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            int off = profile.preferredDelayOffset(kind, x);
            bool const bakePrefer =
                opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin;
            // Density for explore gate even when orb forced us past spam Pass.
            bool const spamDense =
                EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
            if (EbAssistLearning::allowHistogramExplore(kind, spot->histN, spamDense, bakePrefer,
                                                        spot->diedAfterAssist)) {
                // Dev recording + disable-random: keep live/eval deterministic.
                bool const freezeRand = EbDevConfig::recordEnable() &&
                                        EbDevConfig::disableRandom() &&
                                        EbDevCycleRecorder::get().isRecording();
                if (!freezeRand) {
                    off += (std::rand() % 3) - 1;
                }
                EbAssistLog::debug(fmt::format("learn: hist explore ±1 @{} histN={}",
                                               EbLevelProfile::spotBucket(x), spot->histN));
            }
            // After assist deaths at this spot, nudge later — never pull earlier.
            if (spot->diedAfterAssist > 0 && off < 1) {
                off = 1;
            }
            delay = plannedDelay + off;
        }
        // Miss-assist: learning must not undo the planner wait (caused +N then die).
        if (delay < plannedDelay) {
            EbAssistLog::debug(fmt::format("learn: floor delay {}→{} (never earlier than plan)",
                                           delay, plannedDelay));
            delay = plannedDelay;
            learningFloored = true;
        }
        // Clamp bias into activation window when requireOrb
        if (plan.requireOrb && plan.dMin >= 0 && plan.dMax >= plan.dMin) {
            delay = std::max(plan.dMin, std::min(plan.dMax, delay));
            // Still never earlier than plan when plan was already in-window.
            if (plannedDelay >= plan.dMin && delay < plannedDelay) {
                delay = plannedDelay;
                learningFloored = true;
            }
        }
        delay = EbMissAssistContracts::clampDelayToWindow(delay, limit);
        if (delay < 1)
            delay = 1;
        learningNudged = (delay != plannedDelay) && !learningFloored;
        if (learningNudged || learningFloored) {
            EbDevCycleRecorder::get().noteLearningOnPlan(plannedDelay, delay, learningFloored,
                                                         false);
        }
    }

    // Learning may only move to a tick already verified by this plan.
    // AP09: flight/rhythm corrections never move onto an unverified learned tick,
    // and budget-incomplete probes keep the planned delay.
    bool const flightCorr = plan.shipLineCorrection || plan.wavePatternCorrection;
    delay = flightCorr ? EbFlightAssistCombine::guardFlightDelay(delay, plannedDelay,
                                                                plan.viableDelays, plan.budgetHit)
                       : EbSelectedTickGuard::keepVerifiedLearningDelay(delay, plannedDelay,
                                                                        plan.viableDelays);

    // If auto-tune shifted delay away from plannedDelay, verify the shifted delay;
    // when plannedDelay is kept as-is, ShadowSimulator::plan already verified it.
    bool armVerifyOk = true;
    if (delay > 0 && delay != plannedDelay && controller.m_simDt > 0.f) {
        int const learningBudgetUs = frameScope.remaining(frameLimitUs);
        if (learningBudgetUs <= 0) {
            delay = plannedDelay;
        } else {
            float const firstStepDelta =
                (firstDt > 0.f && firstDt <= controller.m_simDt) ? firstDt : controller.m_simDt;
            EbPlanOptions vopts = opts;
            vopts.tickBudget = std::max(200, EbStageConfig::tickBudgetPerPress() / 4);
            vopts.budgetUs = std::min(
                EbLivePlanContracts::intendedLiveBudgetUs(EbStageConfig::husReplanBudgetUs()),
                learningBudgetUs);
            vopts.maxBranches = 0;
            vopts.smartDepth = 1;
            auto const vr = EbShadowSimulator::get().verifyDelay(
                playLayer, player, true, delay, limit, controller.m_simDt, firstStepDelta, vopts);
            if (!vr.ok) {
                EbAssistLog::debug(
                    fmt::format("ASSIST learning shift {}→{} verify-fail → keep plannedDelay={}",
                                plannedDelay, delay, plannedDelay));
                delay = plannedDelay;
            }
            armVerifyOk = vr.ok;
        }
    }

    // Commit 13: wrong-orb reject — activation outside intended window.
    if (plan.requireOrb && EbOrbAlign::wrongOrbReject(delay, plan.dMin, plan.dMax)) {
        EbAssistLog::debug(fmt::format("ASSIST wrong-orb reject delay={} win=[{},{}] → Pass", delay,
                                       plan.dMin, plan.dMax));
        EbSimPlan pass = plan;
        pass.action = PlanAction::Pass;
        pass.delay = 0;
        pass.reason = AssistReason::PassUnknown;
        EbDevCycleRecorder::get().notePassThrough(pass.reason, "press-wrong-orb");
        controller.noteVerdict(pass.reason, "press-wrong-orb");
        controller.recordDecision(playLayer, player, true, pass, effectTick, 0, limit);
        controller.updateOverlay();
        return true;
    }

    controller.m_buffer = {};
    controller.m_buffer.state = BufferState::Buffered;
    controller.m_buffer.down = true;
    controller.m_buffer.firePhase = plan.firePhase;
    controller.m_buffer.shipLineOwned = opts.shipLine.enabled && plan.shipLineCorrection;
    controller.m_buffer.wavePatternOwned =
        opts.wavePattern.enabled && plan.wavePatternCorrection;
    controller.m_buffer.fixedFireTick = plan.fixedFireTick;
    controller.m_buffer.planId = plan.planId;
    controller.m_buffer.player1 = player1;
    controller.m_buffer.pressTick = effectTick;
    controller.m_buffer.planDelay = delay;
    if (controller.m_buffer.shipLineOwned) {
        controller.m_lastShipLineDelay = delay;
        controller.m_lastShipLineTick = effectTick;
        EbAssistLog::info(fmt::format("SHIP-LINE press corr +{}", delay));
    }
    if (controller.m_buffer.wavePatternOwned) {
        controller.m_lastWavePatternDelay = delay;
        controller.m_lastWavePatternTick = effectTick;
        EbAssistLog::info(fmt::format("WAVE-PATTERN press corr +{}", delay));
    }
    controller.m_buffer.limit = limit;
    int const jumpRel = plan.jumpTick > 0 ? plan.jumpTick : delay;
    controller.m_buffer.expectedJumpTick = effectTick + jumpRel;
    controller.m_buffer.fireTick =
        controller.resolveFireTick(effectTick, delay, jumpRel, plan.requireOrb);
    controller.m_buffer.continuous = plan.continuous;
    controller.m_buffer.decisionX = x;
    controller.m_buffer.mode = kind;
    controller.m_buffer.requireOrb = plan.requireOrb;
    controller.m_buffer.requireHold = plan.requireHold;
    controller.m_buffer.releaseAfter = plan.releaseAfter;
    controller.m_buffer.releaseEarliest = plan.releaseEarliest;
    controller.m_buffer.releaseLatest = plan.releaseLatest;
    // Commit 13 orb align: early press waits inside ring window.
    if (plan.requireOrb) {
        controller.m_buffer.dMin = plan.dMin;
        controller.m_buffer.dMax = plan.dMax;
        controller.m_buffer.fireTick = EbOrbAlign::clampFireIntoOrbWindow(
            controller.m_buffer.fireTick, effectTick, plan.dMin, plan.dMax, limit);
        controller.m_buffer.fireTick =
            EbOrbAlign::waitUntilInsideRing(controller.m_buffer.fireTick, effectTick, plan.dMin);
    } else {
        controller.m_buffer.dMin = -1;
        controller.m_buffer.dMax = -1;
    }
    // Never arm past the forgiveness deadline.
    controller.m_buffer.fireTick = std::min(controller.m_buffer.fireTick, effectTick + limit);
    // Non-orb: never fire earlier than planned delay (guards incidental clamps).
    if (!plan.requireOrb && controller.m_buffer.fireTick < effectTick + delay) {
        controller.m_buffer.fireTick = effectTick + delay;
        controller.m_buffer.fireTick = std::min(controller.m_buffer.fireTick, effectTick + limit);
    }

    EbAssistLog::debug(fmt::format("ASSIST press +{} ticks id={} ring={} hold={} rel=[{},{}]@{} "
                                   "win=[{},{}] fire={} jump={} orb={}",
                                   delay, plan.planId, plan.activatedRing, plan.requireHold,
                                   plan.releaseEarliest, plan.releaseLatest, plan.releaseAfter,
                                   controller.m_buffer.dMin, controller.m_buffer.dMax,
                                   controller.m_buffer.fireTick,
                                   controller.m_buffer.expectedJumpTick, plan.requireOrb ? 1 : 0));
    AssistReason delayReason = plan.reason;
    if (!EbAssistVerdict::isDelay(delayReason)) {
        delayReason = EbAssistVerdict::inferDelayReason(
            plan.activatedRing, plan.requireOrb,
            opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin);
    }
    {
        EbDevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = armVerifyOk;
        meta.budgetHitAtArm = plan.budgetHit;
        controller.m_buffer.devPressId = EbDevCycleRecorder::get().noteDelayArm(
            delayReason, "press-delay", delay, controller.m_buffer.fireTick, plan.planId,
            DevEventSource::Live, meta);
    }
    controller.noteVerdict(delayReason, "press-delay");
    if (delay > 0)
        EbAssistRealMetrics::get().noteArm();
    // AP-NL2: Held(+N) + window legality (DR4 rescue / window metrics).
    {
        bool const legal = EbLiveStageParity::windowLegal(
            delay, limit, effectTick, controller.m_buffer.fireTick);
        bool const verified = EbLiveStageParity::delayArmVerified(
            true, plan.budgetHit, plan.unknown || plan.toggleUnknown, false);
        EbAssistRealMetrics::get().noteLiveDecision(EbLiveStageParity::DecisionKind::Held, legal);
        if (!legal || !verified) {
            EbAssistLog::debug(fmt::format(
                "live-decision held legal={} verified={} delay={} fire={} limit={}", legal ? 1 : 0,
                verified ? 1 : 0, delay, controller.m_buffer.fireTick, limit));
        } else {
            EbAssistLog::debug(fmt::format("live-decision {} +{}",
                                           EbLiveStageParity::decisionLabel(
                                               EbLiveStageParity::DecisionKind::Held),
                                           delay));
        }
    }
    EbRealAssistRunner::get().notePress(delay, plan.planUs, plan.hybridTried);
    controller.recordDecision(playLayer, player, true, plan, effectTick, delay, limit);
    controller.updateOverlay();
    return false;
}
