#include "adaptive_plan_search.hpp"
#include "adaptive_budget_controller.hpp"
#include "adaptive_candidate_slot.hpp"
#include "adaptive_decision_gate.hpp"
#include "adaptive_parity_policy.hpp"
#include "adaptive_prediction_scheduler.hpp"
#include "adaptive_resume_runner.hpp"
#include "../../../../../config/stage_config.hpp"
#include "../../../../../debug/perf_trace.hpp"
#include "../../../../assist/contracts/live_plan_contracts.hpp"
#include "../../../../assist/planning/assist_horizon.hpp"
#include "../../../../trigger_world/trigger_world_store.hpp"
#include "../../../prefix_sim.hpp"
#include "../../../sim_session_guard.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../../simulation_candidate.hpp"
#include "../shadow_plan_search.hpp"
#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <vector>

EbAdaptivePlanSearch::ShellStats EbAdaptivePlanSearch::s_lastStats{};

void EbAdaptivePlanSearch::noteStats(ShellStats const& stats) {
    s_lastStats = stats;
}

std::string EbAdaptivePlanSearch::overlayBit() {
    if (!s_lastStats.used)
        return {};
    if (s_lastStats.earlyPass) {
        return fmt::format("adap screen={} surv={} ver={} pass", s_lastStats.screened,
                           s_lastStats.survivors, s_lastStats.verifyGrants);
    }
    if (s_lastStats.gated && s_lastStats.gatedDelay > 0) {
        return fmt::format("adap screen={} surv={} ver={} d{}", s_lastStats.screened,
                           s_lastStats.survivors, s_lastStats.verifyGrants, s_lastStats.gatedDelay);
    }
    if (s_lastStats.gated) {
        return fmt::format("adap screen={} surv={} ver={} gate", s_lastStats.screened,
                           s_lastStats.survivors, s_lastStats.verifyGrants);
    }
    return fmt::format("adap screen={} surv={} ver={}", s_lastStats.screened, s_lastStats.survivors,
                       s_lastStats.verifyGrants);
}

static bool hopelessScreen(std::vector<EbAdaptiveCandidateSlot> const& slots, int limit,
                           int screenedCount, int survivors) {
    if (survivors > 0 || screenedCount <= 0)
        return false;
    if (limit < 0 || slots.empty())
        return false;
    if (!slots[0].terminalDied)
        return false;
    int compared = 0;
    int refDeath = -1;
    for (int delay = 0; delay <= limit && delay < static_cast<int>(slots.size()); ++delay) {
        auto const& slot = slots[static_cast<size_t>(delay)];
        if (slot.ticksSimulated <= 0 && !slot.screened)
            continue;
        if (!slot.terminalDied)
            return false;
        if (slot.terminalUnknown)
            return false;
        int const deathTick = slot.deathTick;
        if (compared == 0)
            refDeath = deathTick;
        else if (deathTick != refDeath)
            return false;
        ++compared;
    }
    return compared >= 2;
}

static int countSurvivors(std::vector<EbAdaptiveCandidateSlot> const& slots, int limit) {
    int survivors = 0;
    for (int delay = 0; delay <= limit && delay < static_cast<int>(slots.size()); ++delay) {
        auto const& slot = slots[static_cast<size_t>(delay)];
        if (slot.autonomousAlive && !slot.terminalDied && !slot.terminalUnknown)
            ++survivors;
    }
    return survivors;
}

EbSimPlan EbAdaptivePlanSearch::run(EbShadowSimulator& simulator, PlayLayer* layer,
                                    PlayerObject* real, bool down, int limit, float deltaTime,
                                    float firstDt, EbPlanOptions const& options) {
    if (!options.adaptivePlan) {
        noteStats({});
        return EbShadowPlanSearch::run(simulator, layer, real, down, limit, deltaTime, firstDt,
                                       options);
    }

    EbPerfTrace::Scope const scope("plan-adaptive");
    static int s_planId = 1;
    EbSimPlan plan;
    plan.smartDepthUsed = 1;
    plan.planId = s_planId++;
    plan.adaptiveShell = true;
    plan.plannerPath = "adaptive";

    ShellStats stats;
    stats.used = true;

    if (!layer || !real || !simulator.m_shadow || limit < 0 || deltaTime <= 0.f) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassInvalidArgs;
        noteStats(stats);
        return plan;
    }
    float useFirstDt = firstDt;
    if (useFirstDt <= 0.f || useFirstDt > deltaTime)
        useFirstDt = deltaTime;

    auto envelope = EbAdaptiveBudgetController::planFromOptions(options);
    // Assist Pack AP-L5: keep Adaptive reserveTicks for classic fallthrough —
    // screen/verify must not burn the whole Stage tick ceiling.
    int const fallthroughReserveTicks = std::max(0, envelope.reserveTicks);
    int sharedTicks = std::max(0, envelope.allowedTicks - fallthroughReserveTicks);
    if (envelope.allowedTicks <= 0) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassBudget;
        plan.budgetHit = true;
        noteStats(stats);
        return plan;
    }
    bool const continuousControl = real->m_isDart || real->m_isShip || real->m_isSwing;
    bool const inputWindow = options.requireAliveForDelay && (down || continuousControl);
    // Assist Pack AP-M7: TW-heavy Wave — smaller screen quantum so Adaptive
    // shell leaves remUs for classic continuous IW fallthrough.
    if (real->m_isDart && continuousControl) {
        envelope.screenQuantum = EbLivePlanContracts::adaptiveWaveScreenTicksForTw(
            envelope.screenQuantum, EbTriggerWorldStore::get().trackCount());
    }
    auto const wall0 = std::chrono::steady_clock::now();
    // Assist Pack AP-L1/L5: Adaptive soft wall leaves fallthrough µs on the
    // shared press budget. Discrete IW (ball/cube) reserves ~half so classic
    // fallthrough can finish ranking (Hoveropolis classic median ~3.8ms).
    int const fallthroughReserveUs = EbLivePlanContracts::adaptiveFallthroughReserveUs(
        envelope.budgetUs, inputWindow && !continuousControl);
    int const adaptiveWallUs =
        EbLivePlanContracts::adaptiveShellBudgetUs(envelope.budgetUs, fallthroughReserveUs);
    simulator.beginPlanWall(wall0, adaptiveWallUs);
    auto wallExceeded = [&]() -> bool { return simulator.markPlanWallIfExceeded(); };

    EbShadowSimulator::Model model = EbShadowSimulator::modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    // Match classic IW: ball stopAtBallLanding so Adaptive quanta share landing
    // semantics with fallthrough ranking (was unset → false). Dual linked
    // exclusion stays in classic prepare; Adaptive solo Live is the ship path.
    model.stopAtBallLanding = inputWindow && real->m_isBall;
    if (inputWindow && EbStageConfig::paramsFor(real).cap == 0 && EbStageConfig::lookAheadCap() == 0) {
        if (continuousControl) {
            // AP3a: same Wave/ship floor as classic continuous IW.
            model.cap = EbAssistHorizon::continuousInputWindowCap(
                EbStageConfig::kindOf(real), limit, options.capMul);
        } else {
            model.cap = std::min(model.cap, std::max(1, 96 - limit));
        }
    }
    if (options.requireAliveForDelay && model.cap > 0) {
        model.cap += EbShadowSurvivalRules::kPostCapProveTicks;
    }

    std::vector<EbAdaptiveCandidateSlot> slots(static_cast<size_t>(limit) + 1);
    int screened = 0;
    int survivors = 0;
    int verifyGrants = 0;
    int prefixDeathTick = -1;
    int sameCrashStreak = 0;
    int prevDeathTick = -1;
    bool earlyPass = false;
    AssistReason earlyReason = AssistReason::None;
    bool fallClassic = false;
    EbThreatZone threatZone{};
    if (options.seedThreatZone.valid)
        threatZone = options.seedThreatZone;

    simulator.m_simTickBase = std::max(0, options.attemptStartTick);
    simulator.m_simAbsTick = -1;

    {
        EbSimSessionGuard session(simulator, layer);

        // Soft wall runs from plan start (same as classic) — screen + verify.
        auto runScreenDelay = [&](int delay) {
            if (delay < 0 || delay > limit)
                return;
            if (wallExceeded()) {
                plan.budgetHit = true;
                return;
            }
            if (envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Screen) <= 0 ||
                sharedTicks <= 0) {
                plan.budgetHit = true;
                return;
            }
            // Assist Pack AP2: prefix-death skips must not charge Screen ticks
            // (mark screened/dead only — budget stays for living survivors / verify).
            if (EbPrefixSim::shouldSkipAfterPrefixDeath(delay, prefixDeathTick)) {
                auto& skipped = slots[static_cast<size_t>(delay)];
                skipped.bindDelay(delay);
                skipped.screened = true;
                skipped.terminalDied = true;
                skipped.deathTick = prefixDeathTick;
                skipped.autonomousSurviveTicks = prefixDeathTick + 1;
                skipped.ticksSimulated = prefixDeathTick + 1;
                ++screened;
                ++plan.candidatesTried;
                return;
            }

            int const quantum =
                std::min(envelope.takeQuantum(EbAdaptiveBudgetController::Phase::Screen),
                         sharedTicks);
            if (quantum <= 0) {
                plan.budgetHit = true;
                return;
            }

            auto& slot = slots[static_cast<size_t>(delay)];
            slot.bindDelay(delay);
            int const ticksBefore = sharedTicks;
            EbThreatZone const* zonePtr = threatZone.valid ? &threatZone : nullptr;
            EbAdaptiveResumeRunner::runQuantum(simulator, layer, real, down, limit, deltaTime,
                                               useFirstDt, model, -1, zonePtr, &sharedTicks, slot,
                                               quantum);
            int const spent = std::max(0, ticksBefore - sharedTicks);
            envelope.spend(EbAdaptiveBudgetController::Phase::Screen, spent);
            simulator.resetCaptureForNextRun(layer);
            slot.invalidateCheckpoint();
            slot.screened = true;
            ++screened;
            ++plan.candidatesTried;

            if (slot.terminalDied && slot.deathTick >= 0 && delay == 0) {
                plan.firstDeathTick = slot.deathTick;
                plan.firstDeathObject = slot.lastRaw.deathObject;
                if (!threatZone.valid) {
                    threatZone = simulator.buildThreatZone(layer, slot.deathX, slot.lastRaw.deathObject);
                    plan.threatZone = threatZone;
                }
            }
            prefixDeathTick = EbPrefixSim::mergePrefixDeathTick(
                prefixDeathTick, slot.deathTick, slot.lastRaw.diedBeforeAction);
            sameCrashStreak = EbPrefixSim::updateSameCrashStreak(
                sameCrashStreak, prevDeathTick, slot.deathTick, slot.terminalDied,
                slot.lastRaw.diedBeforeAction);
            if (slot.terminalDied)
                prevDeathTick = slot.deathTick;
            if (slot.autonomousAlive)
                ++survivors;
        };

        // Phase A — baseline d0
        runScreenDelay(0);
        if (slots[0].terminalDied && slots[0].deathTick >= 0 && plan.firstDeathTick < 0) {
            plan.firstDeathTick = slots[0].deathTick;
            plan.firstDeathObject = slots[0].lastRaw.deathObject;
        }

        // Phase B — screen remaining Max Assist delays
        for (int delay = 1; delay <= limit; ++delay) {
            if (plan.budgetHit)
                break;
            if (EbPrefixSim::sameCrashShouldStop(sameCrashStreak))
                break;
            if (envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Screen) <= 0) {
                // Screen empty with Living survivors is not a hard miss — AP2 will
                // verify with the remaining Verify/Reserve pool (and any steal).
                if (survivors <= 0)
                    plan.budgetHit = true;
                break;
            }
            runScreenDelay(delay);
            // Assist Pack AP2: d0 already dead + later Alive → stop screening more
            // doomed delays; steal leftover Screen into Verify for the save.
            if (slots[0].terminalDied && survivors > 0) {
                envelope.stealScreenToVerify();
                break;
            }
        }

        survivors = countSurvivors(slots, limit);
        // Assist Pack AP2: any Living survivors → leftover Screen becomes Verify
        // (fixed 30% screen share must not starve hazard-progress proof).
        if (survivors > 0)
            envelope.stealScreenToVerify();

        // Soft-wall incomplete screen: prefer classic before declaring hopeless
        // from only a couple same-crash samples.
        if (screened <= 0 ||
            (plan.budgetHit && survivors == 0 && slots[0].terminalDied)) {
            fallClassic = true;
            plan.budgetHit = true;
        } else if (EbLivePlanContracts::adaptiveAbortToFallthroughAfterScreen(
                       inputWindow && !continuousControl, survivors, screened) ||
                   EbLivePlanContracts::adaptiveWaveZeroSurvivorsNeedsClassicIw(
                       real->m_isDart, continuousControl, survivors)) {
            // Assist Pack AP-M6: discrete Adaptive found no Alive — stop here and
            // gift remaining shared remUs to classic (do not burn Verify / gate).
            // Assist Pack AP-M7: Wave Adaptive with zero survivors → classic IW
            // (Grief-class PassBudget seal without hybrid Delay search).
            fallClassic = true;
            plan.adaptiveAbortedForFallthrough = true;
        } else if (hopelessScreen(slots, limit, screened, survivors)) {
            // No Verify on uniformly dead slots — fallthrough keeps sharedTicks.
            earlyPass = true;
            earlyReason = AssistReason::PassHopeless;
        } else if (survivors > 0) {
            // Phase C — adaptive verification (resume-extend within one active delay).
            int activeDelay = -1;
            constexpr int kMaxVerifyLoops = 64;
            for (int loop = 0; loop < kMaxVerifyLoops; ++loop) {
                if (wallExceeded()) {
                    plan.budgetHit = true;
                    break;
                }
                EbAdaptivePredictionScheduler::Input schedIn;
                schedIn.slots = slots.data();
                schedIn.limit = limit;
                schedIn.modelCap = model.cap;
                schedIn.verifyLeft = envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Verify);
                schedIn.reserveLeft = envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Reserve);
                schedIn.mustDelaySearch = options.mustDelaySearch;
                auto const decision = EbAdaptivePredictionScheduler::choose(schedIn);

                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::StopPassD0Safe) {
                    earlyPass = true;
                    earlyReason = AssistReason::PassD0Safe;
                    break;
                }
                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::StopHopeless) {
                    earlyPass = true;
                    earlyReason = AssistReason::PassHopeless;
                    break;
                }
                // StopBudget / StopUnknown: break to DecisionGate with what we
                // screened — never early PassBudget while Alive survivors exist.
                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::StopBudget) {
                    plan.budgetHit = true;
                    break;
                }
                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::StopUnknown) {
                    plan.budgetHit = true;
                    plan.unknown = true;
                    break;
                }
                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::FallThrough) {
                    break;
                }
                if (decision.kind != EbAdaptivePredictionScheduler::ActionKind::ContinueVerify ||
                    decision.delay < 0 || decision.delay > limit) {
                    break;
                }

                int const delay = decision.delay;
                auto& slot = slots[static_cast<size_t>(delay)];

                // Switching candidates requires a clean world; checkpoint cannot
                // survive resetCapture. Metrics from screen stay on the slot.
                if (activeDelay != delay) {
                    if (activeDelay >= 0) {
                        simulator.resetCaptureForNextRun(layer);
                        slots[static_cast<size_t>(activeDelay)].invalidateCheckpoint();
                    }
                    activeDelay = delay;
                }

                bool const useReserve =
                    envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Verify) <= 0;
                auto const phase = useReserve ? EbAdaptiveBudgetController::Phase::Reserve
                                              : EbAdaptiveBudgetController::Phase::Verify;
                int const extendGrant = envelope.takeQuantum(phase);
                // Cold after screen (or after switching away): a short grant from
                // tick 0 never extends past screen depth. Rebuild to screen ticks
                // then add the grant so the checkpoint lands past prior proof.
                int rebuildTicks = 0;
                int grant = extendGrant;
                if (!slot.hasCheckpoint() && slot.ticksSimulated > 0 && slot.autonomousAlive &&
                    !slot.terminalDied && !slot.terminalUnknown) {
                    rebuildTicks = slot.ticksSimulated;
                    grant = rebuildTicks + extendGrant;
                }
                int const quantum = std::min(grant, sharedTicks);
                if (quantum <= 0 || sharedTicks <= 0) {
                    plan.budgetHit = true;
                    break;
                }

                int const ticksBefore = sharedTicks;
                EbThreatZone const* zonePtr = threatZone.valid ? &threatZone : nullptr;
                EbAdaptiveResumeRunner::runQuantum(simulator, layer, real, down, limit, deltaTime,
                                                   useFirstDt, model, -1, zonePtr, &sharedTicks, slot,
                                                   quantum);
                int const spent = std::max(0, ticksBefore - sharedTicks);
                // Hard sharedTicks pay for rebuild+extend; phase bucket only the
                // new extend (rebuild was already charged during Screen).
                int const phaseCharge = rebuildTicks > 0 ? std::max(0, spent - rebuildTicks) : spent;
                envelope.spend(phase, phaseCharge);
                ++slot.verifyGrants;
                ++verifyGrants;
                ++plan.candidatesTried;
                slot.screened = true;

                // Keep world hot for resume-extend of the same delay.
                if (slot.terminalDied || slot.terminalUnknown || !slot.hasCheckpoint()) {
                    simulator.resetCaptureForNextRun(layer);
                    slot.invalidateCheckpoint();
                    activeDelay = -1;
                }
            }

            if (activeDelay >= 0) {
                simulator.resetCaptureForNextRun(layer);
                slots[static_cast<size_t>(activeDelay)].invalidateCheckpoint();
            }
            survivors = countSurvivors(slots, limit);
        }
    }

    stats.screened = screened;
    stats.survivors = survivors;
    stats.verifyGrants = verifyGrants;
    plan.adaptiveScreenedCount = screened;
    plan.adaptiveSurvivors = survivors;
    plan.adaptiveVerifyGrants = verifyGrants;
    plan.adaptiveStolenScreenTicks = envelope.stolenScreenToVerify;

    auto runClassicFallthrough = [&]() -> EbSimPlan {
        plan.adaptiveFellThrough = true;
        noteStats(stats);
        EbPlanOptions classicOpts = options;
        classicOpts.adaptivePlan = false;
        // Assist Pack AP-L5: leftover Adaptive work ticks + reserved fallthrough
        // bucket (never restart a full Stage ceiling — that was ~2× Dynamic).
        classicOpts.tickBudget = std::max(0, sharedTicks) + fallthroughReserveTicks;
        int fallthroughRemUs = 0;
        if (options.budgetUs > 0) {
            auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - wall0)
                                     .count();
            int const remUs = options.budgetUs - static_cast<int>(elapsed);
            bool const waveNeedsClassic =
                EbLivePlanContracts::adaptiveWaveZeroSurvivorsNeedsClassicIw(
                    real->m_isDart, continuousControl, plan.adaptiveSurvivors);
            // Assist Pack AP-M7: Wave zero-survivor gift — classic continuous IW
            // still gets the reserved slice after TW-heavy Adaptive overspend.
            // Non-Wave keeps AP-L5 (no remUs invent past shared wall).
            fallthroughRemUs =
                waveNeedsClassic
                    ? EbLivePlanContracts::adaptiveWaveFallthroughGiftUs(
                          true, remUs, fallthroughReserveUs)
                    : EbLivePlanContracts::intendedFallthroughClassicBudgetUs(
                          remUs, classicOpts.tickBudget);
            if (fallthroughRemUs <= 0) {
                // Shared press wall spent — no remUs floor stack past sim-budget-us.
                EbSimPlan out = plan;
                out.action = PlanAction::Pass;
                out.reason = AssistReason::PassBudget;
                out.budgetHit = true;
                out.adaptiveFellThrough = true;
                out.adaptiveShell = true;
                out.adaptiveAbortedForFallthrough = plan.adaptiveAbortedForFallthrough;
                return out;
            }
            if (EbLivePlanContracts::adaptiveWaveFallthroughUsesFreshWall(waveNeedsClassic,
                                                                          remUs)) {
                // Gifted slice starts a fresh classic wall — reusePlanWall is already
                // past options.budgetUs on Grief-class overspend.
                classicOpts.budgetUs = fallthroughRemUs;
                classicOpts.reusePlanWall = false;
            } else {
                // Reuse Adaptive wall0 + full press budget so classic IW honors the
                // same deadline (Adaptive soft-aborted earlier via shell reserve).
                classicOpts.budgetUs = options.budgetUs;
                classicOpts.reusePlanWall = true;
                classicOpts.planWall0 = wall0;
            }
        }

        // Seed classic with Adaptive terminal / horizon-proven / observation-
        // cleared Alive screens so fallthrough does not re-sim paid work.
        if (threatZone.valid)
            classicOpts.seedThreatZone = threatZone;
        classicOpts.seedPrefixDeathTick = prefixDeathTick;
        // Keep screen quantum on classic opts so IW can accept seeded Alive.
        classicOpts.adaptiveScreenTicks = options.adaptiveScreenTicks;
        classicOpts.seedCandidates.clear();
        classicOpts.seedCandidates.reserve(static_cast<size_t>(limit) + 1);
        int const modelCap = std::max(1, model.cap);
        int const screenQuantum = std::max(1, options.adaptiveScreenTicks);
        bool const seedObservationCleared =
            EbLivePlanContracts::adaptiveFallthroughSeedsObservationCleared(real->m_isBall);
        // Longest known Adaptive death — short screen Alive must not outrank it.
        int knownDeathTick = prefixDeathTick > 0 ? prefixDeathTick : -1;
        for (int delay = 0; delay <= limit; ++delay) {
            auto const& slot = slots[static_cast<size_t>(delay)];
            if (!slot.screened || !slot.terminalDied || slot.deathTick <= 0) {
                continue;
            }
            if (knownDeathTick < 0 || slot.deathTick > knownDeathTick) {
                knownDeathTick = slot.deathTick;
            }
        }
        for (int delay = 0; delay <= limit; ++delay) {
            auto const& slot = slots[static_cast<size_t>(delay)];
            if (!slot.screened)
                continue;
            bool const terminal = slot.terminalDied || slot.terminalUnknown;
            bool const horizonProven =
                slot.autonomousAlive &&
                (slot.terminalAliveCap || slot.ticksSimulated >= modelCap);
            // Observation-cleared Alive: screen quantum + honesty vs known death.
            // Ball: skip — shallow/landing seeds sticky PassD0Safe and block
            // classic stopAtBallLanding Delay ranking (AP-L5).
            bool const observationCleared =
                seedObservationCleared && slot.autonomousAlive && !slot.terminalUnknown &&
                EbLivePlanContracts::adaptiveSeedCountsAlive(slot.ticksSimulated, screenQuantum,
                                                             knownDeathTick, modelCap);
            if (!terminal && !horizonProven && !observationCleared)
                continue;

            EbSimulationCandidate seeded;
            seeded.delay = delay;
            seeded.raw = slot.lastRaw;
            if (seeded.raw.ticksRan < slot.ticksSimulated)
                seeded.raw.ticksRan = slot.ticksSimulated;
            seeded.diedBeforeAction = slot.lastRaw.diedBeforeAction;
            seeded.activatedRing = slot.activatedRing;
            seeded.unknownTrunc = slot.terminalUnknown || slot.lastRaw.unknownTrunc;
            seeded.toggleUnknown = slot.lastRaw.toggleUnknown;
            seeded.endX = slot.endX;
            seeded.deathX = slot.deathX;
            seeded.jumpTick = slot.lastRaw.jumped ? slot.lastRaw.jumpTick : delay;
            seeded.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(seeded.raw);
            seeded.surviveBin = EbShadowSurvivalRules::surviveBinOf(seeded.surviveTicks);
            seeded.releaseAfter = slot.releaseAfter;
            classicOpts.seedCandidates.push_back(seeded);
        }

        int const adaptiveTried = plan.candidatesTried;
        bool const adaptiveBudgetHit = plan.budgetHit;
        auto classicPlan = EbShadowPlanSearch::run(simulator, layer, real, down, limit, deltaTime,
                                                     firstDt, classicOpts);
        classicPlan.adaptiveShell = true;
        classicPlan.adaptiveFellThrough = true;
        classicPlan.adaptiveAbortedForFallthrough = plan.adaptiveAbortedForFallthrough;
        classicPlan.adaptiveScreenedCount = plan.adaptiveScreenedCount;
        classicPlan.adaptiveSurvivors = plan.adaptiveSurvivors;
        classicPlan.adaptiveVerifyGrants = plan.adaptiveVerifyGrants;
        classicPlan.adaptiveStolenScreenTicks = plan.adaptiveStolenScreenTicks;
        // Dev Lab honesty: Adaptive screen/verify work counts toward the press.
        classicPlan.candidatesTried += adaptiveTried;
        // Assist Pack AP-M6: Adaptive shell soft-abort is a remUs gift — do not
        // poison classic IW with budgetHit while fallthrough still has wall left.
        if (adaptiveBudgetHit &&
            EbLivePlanContracts::adaptiveShellHitPoisonsFallthrough(fallthroughRemUs))
            classicPlan.budgetHit = true;
        classicPlan.plannerPath = "hybridFallthrough";
        return classicPlan;
    };

    if (fallClassic)
        return runClassicFallthrough();

    if (earlyPass && EbAdaptiveParityPolicy::shouldFallThroughEarlyPass(earlyReason))
        return runClassicFallthrough();

    if (earlyPass) {
        plan.action = PlanAction::Pass;
        plan.reason = earlyReason;
        plan.delay = 0;
        plan.nowIsSafe = earlyReason == AssistReason::PassD0Safe;
        if (earlyReason == AssistReason::PassD0Safe)
            plan.threatCleared = true;
        stats.earlyPass = true;
        noteStats(stats);
        return plan;
    }

    // Phase D — DecisionGate (honesty). Soft-wall / verify stop still gates
    // screened Alive survivors; incomplete with zero survivors already fell classic.
    std::vector<EbAdaptiveDecisionGate::CandView> views(static_cast<size_t>(limit) + 1);
    for (int delay = 0; delay <= limit; ++delay)
        views[static_cast<size_t>(delay)] =
            EbAdaptiveDecisionGate::fromSlot(slots[static_cast<size_t>(delay)]);

    int const proofTicks =
        options.adaptiveScreenTicks > 0 ? options.adaptiveScreenTicks : 24;
    EbAdaptiveDecisionGate::Input gateIn;
    gateIn.cands = views.data();
    gateIn.limit = limit;
    gateIn.modelCap = model.cap;
    gateIn.robustK = EbShadowSurvivalRules::clampRobustKToWindow(
        EbShadowSurvivalRules::resolveRobustK(real, options), limit);
    gateIn.latencyWeight = std::max(0.f, options.latencyWeight);
    gateIn.mustDelaySearch = options.mustDelaySearch;
    gateIn.budgetHitIncomplete = plan.budgetHit || plan.unknown;
    gateIn.activatedRingPlan = plan.activatedRing;
    gateIn.screenProofTicks = proofTicks;

    auto const gate = EbAdaptiveDecisionGate::decide(gateIn);
    plan.action = gate.action;
    plan.reason = gate.reason;
    plan.delay = gate.delay;
    plan.nowIsSafe = gate.nowIsSafe;
    plan.threatCleared = gate.threatCleared;
    plan.requireOrb = gate.requireOrb;
    plan.jumpTick = gate.jumpTick;
    plan.activatedRing = plan.activatedRing || gate.activatedRing;
    plan.adaptiveFellThrough = false;

    if (EbAdaptiveParityPolicy::shouldFallThroughGate(gate.action == PlanAction::Delay,
                                                      gate.reason))
        return runClassicFallthrough();

    if (gate.action == PlanAction::Delay && gate.delay > 0) {
        plan.viableDelays.clear();
        for (int delay = 0; delay <= limit; ++delay) {
            auto const& view = views[static_cast<size_t>(delay)];
            bool const keep =
                plan.requireOrb
                    ? (EbAdaptiveDecisionGate::isDelayWorthy(view) && view.activatedRing)
                    : EbAdaptiveDecisionGate::hasHazardProgress(view, views[0], model.cap,
                                                                proofTicks);
            if (keep)
                plan.viableDelays.push_back(delay);
        }
        if (!plan.viableDelays.empty()) {
            plan.dMin = plan.viableDelays.front();
            plan.dMax = plan.viableDelays.back();
        }
    }

    stats.gated = true;
    stats.gatedDelay = gate.action == PlanAction::Delay ? gate.delay : 0;
    noteStats(stats);
    return plan;
}

bool EbAdaptivePlanSearch::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    noteStats({});
    expect(overlayBit().empty(), "overlay-empty");

    ShellStats demo;
    demo.used = true;
    demo.screened = 5;
    demo.survivors = 2;
    demo.verifyGrants = 3;
    demo.gated = true;
    demo.gatedDelay = 2;
    noteStats(demo);
    auto const bit = overlayBit();
    expect(bit.find("adap") != std::string::npos, "overlay-adap");
    expect(bit.find("ver=3") != std::string::npos, "overlay-ver");
    expect(bit.find("d2") != std::string::npos, "overlay-d2");

    std::vector<EbAdaptiveCandidateSlot> slots(5);
    for (int delay = 0; delay <= 4; ++delay) {
        slots[static_cast<size_t>(delay)].bindDelay(delay);
        slots[static_cast<size_t>(delay)].screened = true;
        slots[static_cast<size_t>(delay)].terminalDied = true;
        slots[static_cast<size_t>(delay)].deathTick = 3;
        slots[static_cast<size_t>(delay)].ticksSimulated = 4;
    }
    expect(hopelessScreen(slots, 4, 5, 0), "hopeless-yes");
    slots[2].terminalDied = false;
    slots[2].autonomousAlive = true;
    expect(!hopelessScreen(slots, 4, 5, 1), "hopeless-no-surv");

    // Incomplete same-crash sample (<2 compared) must not look hopeless.
    std::vector<EbAdaptiveCandidateSlot> partial(2);
    partial[0].bindDelay(0);
    partial[0].screened = true;
    partial[0].terminalDied = true;
    partial[0].deathTick = 3;
    partial[0].ticksSimulated = 4;
    partial[1].bindDelay(1);
    partial[1].screened = true;
    partial[1].terminalDied = true;
    partial[1].deathTick = 3;
    partial[1].ticksSimulated = 4;
    expect(hopelessScreen(partial, 1, 2, 0), "hopeless-two-same");
    expect(!hopelessScreen(partial, 1, 0, 0), "hopeless-need-screened");

    std::string policyReport;
    expect(EbAdaptiveParityPolicy::selfCheck(policyReport), "parity-policy");
    std::string schedReport;
    expect(EbAdaptivePredictionScheduler::selfCheck(schedReport), "scheduler");
    std::string gateReport;
    expect(EbAdaptiveDecisionGate::selfCheck(gateReport), "decision-gate");
    std::string budgetReport;
    expect(EbAdaptiveBudgetController::selfCheck(budgetReport), "budget");

    // 1.5.1 AP1 hitch policy (wired at fallthrough / press planner).
    expect(EbLivePlanContracts::intendedLiveBudgetUs(0) == EbLivePlanContracts::kHitchCeilingUs,
           "adaptive-zero-wall-clamped");
    expect(EbLivePlanContracts::intendedFallthroughClassicBudgetUs(0, 50) == 0,
           "ap-l5-fallthrough-no-stack");
    expect(EbLivePlanContracts::intendedFallthroughClassicBudgetUs(1500, 50) == 1500,
           "fallthrough-remus-keep");
    expect(EbLivePlanContracts::intendedLiveSoftWallFloorUs(2500) ==
               EbLivePlanContracts::kLiveSoftWallFloorUs,
           "ap-l5-classic-soft-wall-floor");
    expect(EbLivePlanContracts::adaptiveFallthroughReserveUs(8170, true) == 4085 &&
               EbLivePlanContracts::adaptiveShellBudgetUs(
                   8170, EbLivePlanContracts::adaptiveFallthroughReserveUs(8170, true)) == 4085,
           "ap-l5-shell-discrete");
    expect(!EbLivePlanContracts::adaptiveShellHitPoisonsFallthrough(2000) &&
               EbLivePlanContracts::adaptiveAbortToFallthroughAfterScreen(true, 0, 8),
           "ap-m6-remus-gift");
    expect(EbLivePlanContracts::adaptiveWaveZeroSurvivorsNeedsClassicIw(true, true, 0) &&
               EbLivePlanContracts::adaptiveWaveFallthroughGiftUs(true, 0, 2000) == 2000 &&
               EbLivePlanContracts::adaptiveWaveFallthroughUsesFreshWall(true, 0),
           "ap-m7-wave-fallthrough");
    expect(!EbLivePlanContracts::adaptiveFallthroughSeedsObservationCleared(true),
           "ap-l5-ball-no-obs-seed");
    expect(EbLivePlanContracts::peakDeathMarksSkippedNotDead(), "peak-death-skip-not-dead");

    // Overlay pass bit after early-pass stats.
    ShellStats passStats;
    passStats.used = true;
    passStats.screened = 3;
    passStats.survivors = 0;
    passStats.verifyGrants = 0;
    passStats.earlyPass = true;
    noteStats(passStats);
    expect(overlayBit().find("pass") != std::string::npos, "overlay-pass");

    EbPlanOptions off;
    off.adaptivePlan = false;
    expect(!off.adaptivePlan, "flag-off");

    noteStats({});
    if (fails == 0)
        report = "AdaptivePlanSearch OK";
    else
        report = fmt::format("AdaptivePlanSearch {} fails: {}", fails, report);
    return fails == 0;
}
