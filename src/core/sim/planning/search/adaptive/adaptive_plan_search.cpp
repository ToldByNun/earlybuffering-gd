#include "adaptive_plan_search.hpp"
#include "adaptive_budget_controller.hpp"
#include "adaptive_candidate_slot.hpp"
#include "adaptive_prediction_scheduler.hpp"
#include "adaptive_resume_runner.hpp"
#include "../../../../../config/stage_config.hpp"
#include "../../../../../debug/perf_trace.hpp"
#include "../../../prefix_sim.hpp"
#include "../../../sim_session_guard.hpp"
#include "../shadow_plan_search.hpp"
#include <algorithm>
#include <fmt/format.h>
#include <vector>

EbAdaptivePlanSearch::ShellStats EbAdaptivePlanSearch::s_lastStats{};

void EbAdaptivePlanSearch::noteStats(ShellStats const& stats) {
    s_lastStats = stats;
}

std::string EbAdaptivePlanSearch::overlayBit() {
    if (!s_lastStats.used)
        return {};
    char const* tail = "";
    if (s_lastStats.earlyPass)
        tail = " pass";
    else if (s_lastStats.fellThrough)
        tail = " ft";
    return fmt::format("adap screen={} surv={} ver={}{}", s_lastStats.screened,
                       s_lastStats.survivors, s_lastStats.verifyGrants, tail);
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
    int sharedTicks = envelope.allowedTicks;
    if (sharedTicks <= 0) {
        plan.action = PlanAction::Pass;
        plan.reason = AssistReason::PassBudget;
        plan.budgetHit = true;
        noteStats(stats);
        return plan;
    }

    EbShadowSimulator::Model model = EbShadowSimulator::modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    bool const continuousControl = real->m_isDart || real->m_isShip || real->m_isSwing;
    bool const inputWindow = options.requireAliveForDelay && (down || continuousControl);
    if (inputWindow && EbStageConfig::paramsFor(real).cap == 0 && EbStageConfig::lookAheadCap() == 0) {
        if (continuousControl) {
            model.cap = std::max(1, limit);
        } else {
            model.cap = std::min(model.cap, std::max(1, 96 - limit));
        }
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

    simulator.m_simTickBase = std::max(0, options.attemptStartTick);
    simulator.m_simAbsTick = -1;

    {
        EbSimSessionGuard session(simulator, layer);

        auto runScreenDelay = [&](int delay) {
            if (delay < 0 || delay > limit)
                return;
            if (envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Screen) <= 0 ||
                sharedTicks <= 0) {
                plan.budgetHit = true;
                return;
            }
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
            EbAdaptiveResumeRunner::runQuantum(simulator, layer, real, down, limit, deltaTime,
                                               useFirstDt, model, -1, nullptr, &sharedTicks, slot,
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
                plan.budgetHit = true;
                break;
            }
            runScreenDelay(delay);
        }

        survivors = countSurvivors(slots, limit);

        if (hopelessScreen(slots, limit, screened, survivors)) {
            earlyPass = true;
            earlyReason = AssistReason::PassHopeless;
        } else if (screened <= 0 || (plan.budgetHit && survivors == 0 && slots[0].terminalDied)) {
            earlyPass = true;
            earlyReason = AssistReason::PassBudget;
            plan.budgetHit = true;
        } else if (survivors > 0) {
            // Phase C — adaptive verification (resume-extend within one active delay).
            int activeDelay = -1;
            constexpr int kMaxVerifyLoops = 64;
            for (int loop = 0; loop < kMaxVerifyLoops; ++loop) {
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
                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::StopBudget) {
                    earlyPass = true;
                    earlyReason = AssistReason::PassBudget;
                    plan.budgetHit = true;
                    break;
                }
                if (decision.kind == EbAdaptivePredictionScheduler::ActionKind::StopUnknown) {
                    earlyPass = true;
                    earlyReason = AssistReason::PassBudget;
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

                // Switching candidates requires a clean world; checkpoint cannot survive reset.
                if (activeDelay != delay) {
                    if (activeDelay >= 0) {
                        simulator.resetCaptureForNextRun(layer);
                        slots[static_cast<size_t>(activeDelay)].invalidateCheckpoint();
                    }
                    activeDelay = delay;
                    // Fresh verify start from real — keep screen metrics except checkpoint.
                    slot.invalidateCheckpoint();
                    // Preserve delay / screened / prior survive as ranking hints; re-sim from 0.
                    int const keepDelay = slot.delay;
                    bool const keepScreened = slot.screened;
                    int const keepGrants = slot.verifyGrants;
                    slot.bindDelay(keepDelay);
                    slot.screened = keepScreened;
                    slot.verifyGrants = keepGrants;
                }

                bool const useReserve =
                    envelope.phaseLeft(EbAdaptiveBudgetController::Phase::Verify) <= 0;
                auto const phase = useReserve ? EbAdaptiveBudgetController::Phase::Reserve
                                              : EbAdaptiveBudgetController::Phase::Verify;
                int const quantum = std::min(envelope.takeQuantum(phase), sharedTicks);
                if (quantum <= 0 || sharedTicks <= 0) {
                    earlyPass = true;
                    earlyReason = AssistReason::PassBudget;
                    plan.budgetHit = true;
                    break;
                }

                int const ticksBefore = sharedTicks;
                EbAdaptiveResumeRunner::runQuantum(simulator, layer, real, down, limit, deltaTime,
                                                   useFirstDt, model, -1, nullptr, &sharedTicks, slot,
                                                   quantum);
                int const spent = std::max(0, ticksBefore - sharedTicks);
                envelope.spend(phase, spent);
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

    // AP5: DecisionGate not shipped — fall through to classic with remaining budget.
    stats.fellThrough = true;
    plan.adaptiveFellThrough = true;
    noteStats(stats);

    EbPlanOptions rest = options;
    rest.adaptivePlan = false;
    int const remain = envelope.totalLeft();
    if (remain > 0)
        rest.tickBudget = remain;
    else if (sharedTicks > 0)
        rest.tickBudget = sharedTicks;
    else
        rest.tickBudget = std::max(200, options.tickBudget / 4);

    auto classic = EbShadowPlanSearch::run(simulator, layer, real, down, limit, deltaTime, useFirstDt,
                                           rest);
    classic.adaptiveShell = true;
    classic.adaptiveFellThrough = true;
    classic.adaptiveScreenedCount = screened;
    classic.adaptiveSurvivors = survivors;
    classic.adaptiveVerifyGrants = verifyGrants;
    if (plan.firstDeathTick >= 0 && classic.firstDeathTick < 0) {
        classic.firstDeathTick = plan.firstDeathTick;
        classic.firstDeathObject = plan.firstDeathObject;
    }
    return classic;
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
    demo.fellThrough = true;
    noteStats(demo);
    auto const bit = overlayBit();
    expect(bit.find("adap") != std::string::npos, "overlay-adap");
    expect(bit.find("ver=3") != std::string::npos, "overlay-ver");
    expect(bit.find("ft") != std::string::npos, "overlay-ft");

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

    std::string schedReport;
    expect(EbAdaptivePredictionScheduler::selfCheck(schedReport), "scheduler");

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
