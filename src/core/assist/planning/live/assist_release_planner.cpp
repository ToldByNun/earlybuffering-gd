#include "assist_release_planner.hpp"
#include "../../../../config/stage_config.hpp"
#include "../../../../debug/assist_log.hpp"
#include "../../../../debug/lag_probe.hpp"
#include "../../../../debug/perf_trace.hpp"
#include "../../../../dev/capture/dev_clock_contract.hpp"
#include "../../../../dev/capture/dev_cycle_recorder.hpp"
#include "../../../../dev/capture/dev_trace_store.hpp"
#include "../../../../dev/dev_config.hpp"
#include "../../../bake/bake_verifier.hpp"
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
#include "../perfect_assist_policy.hpp"
#include <algorithm>
#include <cstdlib>

bool AssistReleasePlanner::planRelease(AssistController& controller, PlayLayer* playLayer,
                                       PlayerObject* player, bool player1, int effectTick,
                                       float firstDt, DevClockContract::Stamp const& clockIn) {
    DevClockContract::Stamp clock = clockIn;

    // Same rules as purple bake viz: wave/ship/swing/robot, else dash/holdAssist only.
    // Cube/ball/ufo/spider normal UP → Pass (no false release ASSIST).
    if (!controller.shouldPlanRelease(playLayer, player, player1, effectTick)) {
        AssistLog::debug(fmt::format("release: discrete-skip mode={} dashing={} holdAssistBake=0",
                                     StageConfig::kindName(StageConfig::kindOf(player)),
                                     player && player->m_isDashing ? 1 : 0));
        DevCycleRecorder::get().noteGatePass(AssistReason::PassReleaseDiscrete,
                                             "release-discrete-skip");
        controller.noteVerdict(AssistReason::PassReleaseDiscrete, "release-discrete-skip");
        controller.updateOverlay();
        return true;
    }

    int const limit = controller.activeLimit(player);
    if (limit <= 0) {
        DevCycleRecorder::get().noteGatePass(AssistReason::PassLimitZero, "release-limit0");
        controller.noteVerdict(AssistReason::PassLimitZero, "release-limit0");
        return true;
    }

    // GhostParity / SpotTrust: hard Pass before bake (same honesty as press).
    {
        float const xGate = player->getPositionX();
        AssistReason parityReason = AssistReason::None;
        if (SimTrust::get().forcePassReason(xGate, parityReason)) {
            AssistLog::debug(
                fmt::format("SIMTRUST pass release ({})", AssistVerdict::name(parityReason)));
            controller.m_untrustPassHint = true;
            char const* where = parityReason == AssistReason::PassParityDrift
                                    ? "release-parity-drift"
                                    : "release-spot-untrusted";
            DevCycleRecorder::get().noteGatePass(parityReason, where);
            controller.noteVerdict(parityReason, where);
            controller.updateOverlay();
            return true;
        }
    }

    PressSimBudget pressBudget;
    pressBudget.tickLeft = StageConfig::tickBudgetPerPress();
    {
        int const fullUs = StageConfig::simBudgetUs();
        pressBudget.usUnlimited = (fullUs <= 0);
        pressBudget.usLeft = pressBudget.usUnlimited ? 0 : fullUs;
    }
    bool const shareHybrid = StageConfig::assistMode() == AssistMode::Hybrid;

    bool bakeHandled = false;
    bool hybridVerifyAttempted = false;
    HybridPressReuse hybridReuse{};
    bool const bakeRet = controller.tryBakeAssist(
        playLayer, player, player1, false, effectTick, firstDt, bakeHandled,
        shareHybrid ? &pressBudget : nullptr, shareHybrid ? &hybridVerifyAttempted : nullptr,
        shareHybrid ? &hybridReuse : nullptr);
    if (bakeHandled)
        return bakeRet;

    LagProbe::Scope hyLive(shareHybrid ? "eb.hybrid.live" : nullptr,
                           fmt::format("vfy={} remTick={} remUs={}", hybridVerifyAttempted ? 1 : 0,
                                       pressBudget.tickLeft,
                                       pressBudget.usUnlimited ? -1 : pressBudget.usLeft));

    auto const kind = StageConfig::kindOf(player);
    float const x = player->getPositionX();
    auto& profile = LevelProfile::get();
    int const slot = player1 ? 0 : 1;
    bool const nearOrb = AssistControllerImplementation::nearOrbOrPadHint(playLayer, player, limit,
                                                                          controller.m_simDt);
    bool nearBake = AssistControllerImplementation::nearBakeWindowHint(
        player, player1, false, effectTick, controller.m_simDt,
        shareHybrid ? &hybridReuse : nullptr);
    if (hybridVerifyAttempted)
        nearBake = false;

    if (DivergenceTracker::get().simUntrusted()) {
        if (!nearOrb) {
            AssistLog::debug("UNTRUST pass (release)");
            controller.m_untrustPassHint = true;
            DevCycleRecorder::get().noteGatePass(AssistReason::PassUntrust, "release-untrust");
            controller.noteVerdict(AssistReason::PassUntrust, "release-untrust");
            controller.updateOverlay();
            return true;
        }
        AssistLog::debug("UNTRUST soft (release orb foresight — allow plan)");
        controller.m_untrustPassHint = false;
    } else {
        controller.m_untrustPassHint = false;
    }

    // Commit 11/13: same PerfectAssist as press — trust+opportunity must Delay-search
    // (wave hold-longer saves must not lose to spam / PassNoThreat).
    bool mustDelaySearch = false;
    {
        PerfectAssistPolicy::TrustInput trustIn;
        trustIn.parityDrift = SimTrust::get().parityDrift();
        trustIn.spotUntrusted = SpotTrustMap::get().isUntrusted(x);
        trustIn.divergenceUntrusted = DivergenceTracker::get().simUntrusted() && !nearOrb;
        if (StageConfig::triggerBake()) {
            auto& tw = TriggerWorldStore::get();
            auto& baker = TriggerWorldBaker::get();
            trustIn.twEmptyWhenExpected =
                tw.loaded() && !tw.hasTracks() && !baker.running() &&
                !baker.lastCollectIncomplete() &&
                (baker.lastBakedEffects() > 0 || baker.lastBakedStartTimed() > 0);
        }
        AssistReason namedPass = AssistReason::None;
        if (PerfectAssistPolicy::forcePassReason(trustIn, namedPass) &&
            namedPass == AssistReason::PassTwEmpty) {
            AssistLog::debug("PERFECT pass-tw-empty (release)");
            controller.m_untrustPassHint = true;
            DevCycleRecorder::get().noteGatePass(AssistReason::PassTwEmpty, "release-tw-empty");
            controller.noteVerdict(AssistReason::PassTwEmpty, "release-tw-empty");
            controller.updateOverlay();
            return true;
        }
        PerfectAssistPolicy::OpportunityInput oppIn;
        oppIn.foresightOrbPad = nearOrb;
        oppIn.bakePreferBand = nearBake;
        oppIn.orbWindowFound = nearOrb;
        auto const decision = PerfectAssistPolicy::classify(trustIn, oppIn);
        mustDelaySearch = (decision == PerfectAssistPolicy::Decision::MustDelaySearch);
        if (mustDelaySearch) {
            AssistLog::debug("PERFECT must-delay-search release (trust+opportunity)");
        }
    }

    // Continuous spam: do not delay releases (would steal hold rhythm) unless
    // PerfectAssist demands a Delay-search (orb/bake opportunity).
    bool const continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
    if (continuous && !mustDelaySearch) {
        if (ClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind, nearOrb, nearBake)) {
            AssistLog::debug(fmt::format("intent: spam Pass release (mode={}, tick={})",
                                         StageConfig::kindName(kind), effectTick));
            DevCycleRecorder::get().noteGatePass(AssistReason::PassSpam, "release-spam");
            controller.noteVerdict(AssistReason::PassSpam, "release-spam");
            controller.updateOverlay();
            return true;
        }
    }

    if (!mustDelaySearch && StageConfig::autoTune() && profile.loaded() && !nearOrb) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            if (spot->n >= LevelProfile::TRUST_PASS_MIN_N &&
                spot->trust < LevelProfile::TRUST_PASS_THRESHOLD &&
                !AssistLearning::shouldExploreDespiteLowTrust(spot->trust)) {
                AssistLog::debug(
                    fmt::format("learned: low trust @{} (release)", LevelProfile::spotBucket(x)));
                AssistLearning::decaySpot(kind, x);
                DevCycleRecorder::get().noteGatePass(AssistReason::PassLowTrust,
                                                     "release-low-trust");
                controller.noteVerdict(AssistReason::PassLowTrust, "release-low-trust");
                controller.updateOverlay();
                return true;
            }
        }
    }

    PlanOptions opts;
    if (shareHybrid) {
        opts.tickBudget = std::max(0, pressBudget.tickLeft);
        if (pressBudget.usUnlimited) {
            opts.budgetUs = 0;
        } else {
            opts.budgetUs = std::max(1, pressBudget.usLeft);
        }
    } else {
        opts.budgetUs = StageConfig::simBudgetUs();
        opts.tickBudget = StageConfig::tickBudgetPerPress();
    }
    if (shareHybrid && hybridVerifyAttempted) {
        int const halfTick = std::max(400, StageConfig::tickBudgetPerPress() / 2);
        opts.tickBudget = std::min(opts.tickBudget, halfTick);
        if (!pressBudget.usUnlimited) {
            int const fullUs = StageConfig::simBudgetUs();
            int const halfUs = std::max(200, fullUs / 2);
            opts.budgetUs = std::max(1, std::min(opts.budgetUs, halfUs));
        }
    }
    opts.capMul = StageConfig::autoTune() ? profile.capMul(kind) : 1.f;
    if (DivergenceTracker::get().shouldWarn()) {
        opts.capMul *= 0.85f;
    }
    opts.maxBranches = 4;
    opts.attemptStartTick = effectTick;
    opts.legacyPick = StageConfig::assistPickLegacy();
    opts.assistEngine = StageConfig::effectiveAssistEngine();
    opts.mustDelaySearch = mustDelaySearch;
    // Live Delay-arm: only Alive ghosts (bake verify keeps soft Landing/LateCrash).
    opts.requireAliveForDelay = true;
    bool const spamDenseRel = ClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
    AssistControllerImplementation::fillPlanRobustLatency(opts, player, controller.m_dtVarEma,
                                                          spamDenseRel);
    if (!hybridVerifyAttempted) {
        AssistControllerImplementation::fillBakePreferBand(
            opts, player, player1, false, effectTick, limit, controller.m_attemptTick,
            controller.m_simDt, shareHybrid ? &hybridReuse : nullptr);
    }
    if (hybridVerifyAttempted && hybridReuse.verifyThreat.valid) {
        opts.seedThreatZone = hybridReuse.verifyThreat;
    }

    auto plan = ShadowSimulator::get().plan(playLayer, player, false, limit, controller.m_simDt,
                                            firstDt, opts);
    clock.liveSimPathId = DevSimPathId::Plan;
    clock.effectTick = effectTick;
    DevCycleRecorder::get().notePlanFinished(plan, effectTick, limit, clock, playLayer, player);

    // Belt: never arm Delay when sim marked unknown / toggle-target uncertainty.
    if (plan.action == PlanAction::Delay && (plan.unknown || plan.toggleUnknown)) {
        AssistLog::debug(
            fmt::format("release: refuse Delay (toggle/unknown) → Pass id={}", plan.planId));
        plan.action = PlanAction::Pass;
        plan.delay = 0;
        plan.reason = AssistReason::PassToggleUnknown;
        plan.unknown = true;
        plan.toggleUnknown = true;
    }

    if (plan.action != PlanAction::Delay) {
        AssistReason passReason = plan.reason;
        if (passReason == AssistReason::None) {
            passReason = AssistReason::PassUnknown;
        }
        DevCycleRecorder::get().notePassThrough(passReason, "release-plan");
        controller.noteVerdict(passReason, "release-plan");
        controller.recordDecision(playLayer, player, false, plan, effectTick, 0, limit);
        return true;
    }

    int delay = plan.delay;
    int const plannedDelay = plan.delay;
    if (StageConfig::autoTune() && profile.loaded()) {
        if (auto const* spot = profile.trySpot(kind, x)) {
            int off = profile.preferredDelayOffset(kind, x);
            bool const bakePrefer =
                opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin;
            bool const spamDense =
                ClickIntentGuard::get().shouldPassThrough(slot, effectTick, kind);
            if (AssistLearning::allowHistogramExplore(kind, spot->histN, spamDense, bakePrefer,
                                                      spot->diedAfterAssist)) {
                bool const freezeRand = DevConfig::recordEnable() && DevConfig::disableRandom() &&
                                        DevCycleRecorder::get().isRecording();
                if (!freezeRand) {
                    off += (std::rand() % 3) - 1;
                }
                AssistLog::debug(fmt::format("learn: hist explore ±1 release @{} histN={}",
                                             LevelProfile::spotBucket(x), spot->histN));
            }
            if (spot->diedAfterAssist > 0 && off < 1) {
                off = 1;
            }
            delay = plannedDelay + off;
        }
        if (delay < plannedDelay) {
            delay = plannedDelay;
        }
        delay = MissAssistContracts::clampDelayToWindow(delay, limit);
        if (delay < 1)
            delay = 1;
    }

    controller.m_buffer = {};
    controller.m_buffer.state = BufferState::Buffered;
    controller.m_buffer.down = false;
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

    AssistLog::debug(
        fmt::format("ASSIST release +{} ticks (now dies @+{}, limit {}, tick={} fire={})", delay,
                    plan.firstDeathTick, limit, effectTick, controller.m_buffer.fireTick));
    AssistReason delayReason = plan.reason;
    if (!AssistVerdict::isDelay(delayReason)) {
        delayReason = AssistVerdict::inferDelayReason(
            plan.activatedRing, plan.requireOrb,
            opts.preferDelayMin >= 0 && opts.preferDelayMax >= opts.preferDelayMin);
    }
    {
        DevCycleRecorder::DelayArmMeta meta{};
        meta.armVerifyOk = true;
        meta.budgetHitAtArm = plan.budgetHit;
        controller.m_buffer.devPressId = DevCycleRecorder::get().noteDelayArm(
            delayReason, "release-delay", delay, controller.m_buffer.fireTick, plan.planId,
            DevEventSource::Live, meta);
    }
    controller.noteVerdict(delayReason, "release-delay");
    if (delay > 0)
        AssistRealMetrics::get().noteArm();
    controller.recordDecision(playLayer, player, false, plan, effectTick, delay, limit);
    controller.updateOverlay();
    return false;
}
