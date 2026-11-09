#include "assist_press_planner.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../debug/assist_log.hpp"
#include "../../../../debug/lag_probe.hpp"
#include "../../../../debug/perf_trace.hpp"
#include "../../../../dev/capture/dev_clock_contract.hpp"
#include "../../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../../dev/capture/dev_trace_store.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../bake/bake_verifier.hpp"
#include "../../../diagnostics/assist_perf_stats.hpp"
#include "../../../diagnostics/sim_tick_cost_meter.hpp"
#include "../../../diagnostics/assist_real_metrics.hpp"
#include "../../../diagnostics/divergence_tracker.hpp"
#include "../../../learning/assist_learning.hpp"
#include "../../../learning/level_profile.hpp"
#include "../../../learning/spot_trust_map.hpp"
#include "../../../sim/planning/simulation_plan.hpp"
#include "../../../sim/shadow_simulator.hpp"
#include "../../../sim/sim_trust.hpp"
#include "../../../trigger_world/trigger_world_baker.hpp"
#include "../../../trigger_world/trigger_world_store.hpp"
#include "../../assist_controller.hpp"
#include "../../contracts/assist_verdict.hpp"
#include "../../contracts/miss_assist_contracts.hpp"
#include "../../input/click_intent_guard.hpp"
#include "../orb_align.hpp"
#include "../perfect_assist_policy.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>

bool EbAssistPressPlanner::planPress(EbAssistController& controller, PlayLayer* playLayer,
                                   PlayerObject* player, bool player1, int effectTick,
                                   float firstDt, EbDevClockContract::Stamp const& clockIn) {
    EbDevClockContract::Stamp clock = clockIn;
    int const limit = controller.activeLimit(player);
    if (limit <= 0) {
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

    // GhostParity / SpotTrust: hard Pass before bake+Live — never soft with orb.
    // Commit 11: reasons via PerfectAssistPolicy / SimTrust (same tokens).
    {
        float const xGate = player->getPositionX();
        AssistReason parityReason = AssistReason::None;
        if (EbSimTrust::get().forcePassReason(xGate, parityReason)) {
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

    // Hybrid: one press wall/tick envelope for bake verify + Live fallthrough.
    EbPressSimBudget pressBudget;
    pressBudget.tickLeft = EbStageConfig::tickBudgetPerPress();
    {
        int const fullUs = EbStageConfig::simBudgetUs();
        pressBudget.usUnlimited = (fullUs <= 0);
        pressBudget.usLeft = pressBudget.usUnlimited ? 0 : fullUs;
    }
    bool const shareHybrid = EbStageConfig::assistMode() == AssistMode::Hybrid;

    bool bakeHandled = false;
    bool hybridVerifyAttempted = false;
    EbHybridPressReuse hybridReuse{};
    bool const bakeRet = controller.tryBakeAssist(
        playLayer, player, player1, true, effectTick, firstDt, bakeHandled,
        shareHybrid ? &pressBudget : nullptr, shareHybrid ? &hybridVerifyAttempted : nullptr,
        shareHybrid ? &hybridReuse : nullptr);
    if (bakeHandled)
        return bakeRet;

    // Times Live fallthrough (verify already paid / skipped). Standalone LagProbe.
    EbLagProbe::Scope hyLive(shareHybrid ? "eb.hybrid.live" : nullptr,
                           fmt::format("vfy={} remTick={} remUs={}", hybridVerifyAttempted ? 1 : 0,
                                       pressBudget.tickLeft,
                                       pressBudget.usUnlimited ? -1 : pressBudget.usLeft));

    auto const kind = EbStageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = EbLevelProfile::get();
    int const slot = player1 ? 0 : 1;
    bool const nearOrb = EbAssistControllerImplementation::nearOrbOrPadHint(playLayer, player, limit,
                                                                          controller.m_simDt);
    // After Hybrid bake verify rejected the window, do not treat near-bake as
    // spam override — that forced full Live on every ~5 CPS click (main-thread hitch).
    // Reuse findNear result from tryBakeAssist when present (no second lookup).
    bool nearBake = EbAssistControllerImplementation::nearBakeWindowHint(
        player, player1, true, effectTick, controller.m_simDt,
        shareHybrid ? &hybridReuse : nullptr);
    if (hybridVerifyAttempted)
        nearBake = false;

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
    bool mustDelaySearch = false;
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
        oppIn.bakePreferBand = nearBake;
        oppIn.orbWindowFound = nearOrb;
        auto const decision = EbPerfectAssistPolicy::classify(trustIn, oppIn);
        mustDelaySearch = (decision == EbPerfectAssistPolicy::Decision::MustDelaySearch);
        if (mustDelaySearch) {
            EbAssistLog::debug("PERFECT must-delay-search (trust+opportunity)");
        }
    }

    // Dense / rhythmic spam (esp. wave corridors): Pass unless orb/pad or bake nearby.
    // Trusted opportunity must still Delay-search — never spam-Pass over AssistCov.
    if (!mustDelaySearch &&
        EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind, nearOrb, nearBake)) {
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
                                             EbLevelProfile::spotBucket(x), spot->trust, spot->n));
                EbAssistLearning::decaySpot(kind, x);
                EbSimPlan dummy;
                dummy.action = PlanAction::Pass;
                dummy.reason = AssistReason::PassLowTrust;
                dummy.firstDeathTick = -1;
                EbDevCycleRecorder::get().noteGatePass(AssistReason::PassLowTrust, "press-low-trust");
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
    if (shareHybrid) {
        opts.tickBudget = std::max(0, pressBudget.tickLeft);
        if (pressBudget.usUnlimited) {
            opts.budgetUs = 0;
        } else {
            opts.budgetUs = std::max(1, pressBudget.usLeft);
        }
    } else {
        opts.budgetUs = EbStageConfig::simBudgetUs();
        opts.tickBudget = EbStageConfig::tickBudgetPerPress();
    }
    // Verify already paid this press: hard-cap Live so verify-fail + full plan
    // cannot hitch the game (~0.75 feel / stutter at moderate CPS).
    if (shareHybrid && hybridVerifyAttempted) {
        int const halfTick = std::max(400, EbStageConfig::tickBudgetPerPress() / 2);
        opts.tickBudget = std::min(opts.tickBudget, halfTick);
        if (!pressBudget.usUnlimited) {
            int const fullUs = EbStageConfig::simBudgetUs();
            int const halfUs = std::max(200, fullUs / 2);
            opts.budgetUs = std::min(opts.budgetUs, halfUs);
            opts.budgetUs = std::max(1, opts.budgetUs);
        }
    }
    opts.capMul = EbStageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    // Soft warn: shorten look-ahead slightly (no hard Pass).
    if (EbDivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;
    opts.smartDepth = EbStageConfig::smartDepth();
    opts.smartCombos = EbStageConfig::smartCombos();
    opts.smartHorizon = EbStageConfig::smartHorizon();
    // Adaptive contracts only (AP0). Search still classic until AdaptivePlanSearch.
    opts.adaptivePlan = EbStageConfig::adaptivePlan();
    opts.adaptiveScreenTicks = EbStageConfig::adaptiveScreenTicks();
    opts.adaptiveVerifyQuantum = EbStageConfig::adaptiveVerifyQuantum();
    opts.adaptiveReservePct = EbStageConfig::adaptiveReservePct();
    opts.attemptStartTick = effectTick;
    opts.legacyPick = EbStageConfig::assistPickLegacy();
    opts.assistEngine = EbStageConfig::effectiveAssistEngine();
    opts.mustDelaySearch = mustDelaySearch;
    // Live Delay-arm: only Alive ghosts (bake verify keeps soft Landing/LateCrash).
    opts.requireAliveForDelay = true;
    // Density without orb/bake override — softens Wave λ / bumps auto k when
    // we still assist (e.g. orb in spam corridor).
    bool const spamDense = EbClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
    EbAssistControllerImplementation::fillPlanRobustLatency(opts, player, controller.m_dtVarEma,
                                                          spamDense);
    // Rejected bake window must not steer Live prefer-band (wasted sims / hitch).
    // Otherwise reuse findNear window fields from tryBakeAssist.
    if (!hybridVerifyAttempted) {
        EbAssistControllerImplementation::fillBakePreferBand(
            opts, player, player1, true, effectTick, limit, controller.m_attemptTick,
            controller.m_simDt, shareHybrid ? &hybridReuse : nullptr);
    }
    // Verify-fail: seed threat zone so plan() skips buildThreatZone.
    if (hybridVerifyAttempted && hybridReuse.verifyThreat.valid) {
        opts.seedThreatZone = hybridReuse.verifyThreat;
    }

    auto const t0 = std::chrono::steady_clock::now();
    // planSequence only when Smart Depth > 1 (New depth-1 exit-predict removed).
    // After Hybrid verify, skip when wall remainder is tight.
    bool useSequence = opts.smartDepth > 1;
    if (hybridVerifyAttempted && !pressBudget.usUnlimited && opts.budgetUs < 600) {
        useSequence = false;
    }
    auto plan = useSequence ? EbShadowSimulator::get().planSequence(playLayer, player, true, limit,
                                                                  controller.m_simDt, firstDt, opts)
                            : EbShadowSimulator::get().plan(playLayer, player, true, limit,
                                                          controller.m_simDt, firstDt, opts);
    int const planUs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count());
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

    if (plan.action != PlanAction::Delay) {
        AssistReason passReason = plan.reason;
        if (passReason == AssistReason::None) {
            passReason = AssistReason::PassUnknown;
        }
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
                bool const freezeRand = EbDevConfig::recordEnable() && EbDevConfig::disableRandom() &&
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
            EbDevCycleRecorder::get().noteLearningOnPlan(plannedDelay, delay, learningFloored, false);
        }
    }

    // If auto-tune shifted delay away from plannedDelay, verify the shifted delay;
    // when plannedDelay is kept as-is, ShadowSimulator::plan already verified it.
    bool armVerifyOk = true;
    if (delay > 0 && delay != plannedDelay && controller.m_simDt > 0.f) {
        float const firstStepDelta =
            (firstDt > 0.f && firstDt <= controller.m_simDt) ? firstDt : controller.m_simDt;
        EbPlanOptions vopts = opts;
        vopts.tickBudget = std::max(200, EbStageConfig::tickBudgetPerPress() / 4);
        vopts.budgetUs = EbStageConfig::husReplanBudgetUs();
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
    controller.m_buffer.fixedFireTick = plan.fixedFireTick;
    controller.m_buffer.planId = plan.planId;
    controller.m_buffer.player1 = player1;
    controller.m_buffer.pressTick = effectTick;
    controller.m_buffer.planDelay = delay;
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
                                 controller.m_buffer.fireTick, controller.m_buffer.expectedJumpTick,
                                 plan.requireOrb ? 1 : 0));
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
    controller.recordDecision(playLayer, player, true, plan, effectTick, delay, limit);
    controller.updateOverlay();
    return false;
}
