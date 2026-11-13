#include "adaptive_prediction_scheduler.hpp"
#include "../../../../../config/stage_config.hpp"
#include "../../../../assist/contracts/live_plan_contracts.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

int EbAdaptivePredictionScheduler::scoreCandidate(EbAdaptiveCandidateSlot const& slot,
                                                  EbAdaptiveCandidateSlot const& d0, int modelCap) {
    if (!slot.screened)
        return -1;
    if (slot.terminalDied || slot.terminalUnknown || !slot.autonomousAlive)
        return -1;
    int const cap = std::max(1, modelCap);
    if (slot.ticksSimulated >= cap || slot.terminalAliveCap)
        return -2; // fully verified to horizon — not a grant target
    int gain = cap - slot.ticksSimulated;
    // Beating a dying d0 is the highest-value proof Adaptive can chase.
    if (d0.terminalDied && slot.autonomousAlive)
        gain += 1000;
    if (slot.zoneCleared)
        gain += 100;
    // Prefer candidates that have received fewer verify grants (fairness / info).
    gain -= slot.verifyGrants * 40;
    gain += std::min(slot.autonomousSurviveTicks, cap);
    // Slight prefer earlier delay when scores tie later (resolved by scan order).
    gain -= slot.delay;
    return gain;
}

EbAdaptivePredictionScheduler::Decision EbAdaptivePredictionScheduler::choose(Input const& in) {
    Decision out;
    if (!in.slots || in.limit < 0)
        return out;

    auto const& d0 = in.slots[0];
    int const cap = std::max(1, in.modelCap);

    // Recount living / horizon-complete survivors from current metrics.
    int living = 0;
    int atHorizon = 0;
    int bestLivingSurvive = -1;
    int bestLivingDelay = -1;
    for (int delay = 0; delay <= in.limit; ++delay) {
        auto const& slot = in.slots[delay];
        if (!slot.screened)
            continue;
        if (slot.terminalDied || slot.terminalUnknown || !slot.autonomousAlive)
            continue;
        ++living;
        if (slot.ticksSimulated >= cap || slot.terminalAliveCap)
            ++atHorizon;
        if (slot.autonomousSurviveTicks > bestLivingSurvive) {
            bestLivingSurvive = slot.autonomousSurviveTicks;
            bestLivingDelay = delay;
        }
    }

    if (living <= 0) {
        out.kind = ActionKind::StopHopeless;
        return out;
    }

    // d0 already proved through horizon and nothing must-search → Pass.
    // Assist Pack AP3: under-horizon / endX-leading later Alive blocks terminal
    // StopPassD0Safe so verify / classic can finish the screen.
    if (!in.mustDelaySearch && d0.autonomousAlive && !d0.terminalDied && !d0.terminalUnknown &&
        (d0.ticksSimulated >= cap || d0.terminalAliveCap)) {
        bool laterBetter = false;
        for (int delay = 1; delay <= in.limit; ++delay) {
            auto const& slot = in.slots[delay];
            if (!slot.autonomousAlive || slot.terminalDied || slot.terminalUnknown)
                continue;
            bool const underHorizon = !slot.terminalAliveCap && slot.ticksSimulated < cap;
            if (EbLivePlanContracts::laterAliveBlocksPassD0Safe(
                    true, underHorizon, slot.autonomousSurviveTicks, d0.autonomousSurviveTicks,
                    in.closeGapTicks, slot.endX, d0.endX)) {
                laterBetter = true;
                break;
            }
        }
        // Assist Pack AP3c: wall-safe d0 + later Alive → keep verifying / fall
        // through instead of terminal PassD0Safe (setting-gated).
        if (!laterBetter) {
            bool anyLaterAlive = false;
            for (int delay = 1; delay <= in.limit; ++delay) {
                auto const& slot = in.slots[delay];
                if (slot.autonomousAlive && !slot.terminalDied && !slot.terminalUnknown) {
                    anyLaterAlive = true;
                    break;
                }
            }
            bool const atWall = EbLivePlanContracts::d0AtProveWall(
                true, false, d0.ticksSimulated, cap);
            if (EbLivePlanContracts::horizonProbeBlocksPassD0Safe(
                    EbStageConfig::assistProbePastHorizon(), atWall, anyLaterAlive)) {
                out.kind = ActionKind::FallThrough;
                return out;
            }
            out.kind = ActionKind::StopPassD0Safe;
            return out;
        }
    }

    int const verifyPool = std::max(0, in.verifyLeft) + std::max(0, in.reserveLeft);
    if (verifyPool <= 0) {
        // Horizon-proved survivor exists → fall through / gate; else unknown.
        if (atHorizon > 0) {
            out.kind = ActionKind::FallThrough;
            return out;
        }
        out.kind = ActionKind::StopUnknown;
        return out;
    }

    // Pick highest information-gain under-horizon living candidate.
    int bestScore = -1;
    int bestDelay = -1;
    int secondScore = -1;
    int secondDelay = -1;
    for (int delay = 0; delay <= in.limit; ++delay) {
        int const score = scoreCandidate(in.slots[delay], d0, cap);
        if (score < 0)
            continue;
        if (score > bestScore) {
            secondScore = bestScore;
            secondDelay = bestDelay;
            bestScore = score;
            bestDelay = delay;
        } else if (score > secondScore) {
            secondScore = score;
            secondDelay = delay;
        }
    }

    if (bestDelay < 0) {
        // Everyone living is already at horizon.
        out.kind = ActionKind::FallThrough;
        return out;
    }

    // Close race: prefer the less-verified of the top two.
    if (secondDelay >= 0) {
        auto const& a = in.slots[bestDelay];
        auto const& b = in.slots[secondDelay];
        int const gap = std::abs(a.autonomousSurviveTicks - b.autonomousSurviveTicks);
        if (gap <= std::max(0, in.closeGapTicks) && b.verifyGrants < a.verifyGrants) {
            bestDelay = secondDelay;
            bestScore = secondScore;
        }
    }

    // Single clear Alive lead past a dead d0 and already deep → fall through to gate.
    if (d0.terminalDied && living == 1 && bestLivingDelay == bestDelay &&
        in.slots[bestDelay].ticksSimulated >= std::max(cap / 2, 24) &&
        in.slots[bestDelay].verifyGrants >= 1) {
        out.kind = ActionKind::FallThrough;
        out.delay = bestDelay;
        return out;
    }

    out.kind = ActionKind::ContinueVerify;
    out.delay = bestDelay;
    (void)bestScore;
    return out;
}

bool EbAdaptivePredictionScheduler::selfCheck(std::string& report) {
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

    EbAdaptiveCandidateSlot slots[5];
    for (int delay = 0; delay <= 4; ++delay) {
        slots[delay].bindDelay(delay);
        slots[delay].screened = true;
        slots[delay].terminalDied = true;
        slots[delay].deathTick = 2;
        slots[delay].autonomousSurviveTicks = 3;
        slots[delay].ticksSimulated = 3;
    }
    Input hopelessIn;
    hopelessIn.slots = slots;
    hopelessIn.limit = 4;
    hopelessIn.modelCap = 64;
    hopelessIn.verifyLeft = 100;
    expect(choose(hopelessIn).kind == ActionKind::StopHopeless, "hopeless");

    slots[0].terminalDied = false;
    slots[0].autonomousAlive = true;
    slots[0].ticksSimulated = 64;
    slots[0].terminalAliveCap = true;
    slots[0].autonomousSurviveTicks = 64;
    slots[0].endX = 100.f;
    slots[2].terminalDied = false;
    slots[2].autonomousAlive = true;
    slots[2].ticksSimulated = 20;
    slots[2].autonomousSurviveTicks = 20;
    slots[2].verifyGrants = 0;
    slots[2].endX = 100.f;
    Input d0SafeIn = hopelessIn;
    d0SafeIn.mustDelaySearch = false;
    // AP3c probe (default on): wall-safe d0 + later Alive → FallThrough, not sticky.
    expect(choose(d0SafeIn).kind == ActionKind::FallThrough, "ap3c-probe-ft");
    // Clear endX lead on under-horizon later → keep verifying, not terminal.
    slots[2].endX = 120.f;
    expect(choose(d0SafeIn).kind == ActionKind::ContinueVerify, "ap3-endx-lead-verify");
    expect(choose(d0SafeIn).delay == 2, "ap3-verify-later");
    slots[2].endX = 100.f;
    // Peer also at horizon — probe still falls through (later Alive at wall).
    slots[2].ticksSimulated = 64;
    slots[2].terminalAliveCap = true;
    slots[2].autonomousSurviveTicks = 64;
    expect(choose(d0SafeIn).kind == ActionKind::FallThrough, "ap3c-probe-peers-ft");
    // No later Alive → sticky PassD0Safe even with probe on.
    slots[2].terminalDied = true;
    slots[2].autonomousAlive = false;
    expect(choose(d0SafeIn).kind == ActionKind::StopPassD0Safe, "ap3c-probe-alone-sticky");
    slots[2].terminalDied = false;
    slots[2].autonomousAlive = true;
    // Restore under-horizon later for the verify-continue cases below.
    slots[2].ticksSimulated = 20;
    slots[2].terminalAliveCap = false;
    slots[2].autonomousSurviveTicks = 20;

    slots[0].ticksSimulated = 10;
    slots[0].terminalAliveCap = false;
    slots[0].autonomousSurviveTicks = 10;
    slots[0].terminalDied = true;
    slots[0].autonomousAlive = false;
    Input verifyIn = hopelessIn;
    verifyIn.verifyLeft = 50;
    auto const verify = choose(verifyIn);
    expect(verify.kind == ActionKind::ContinueVerify, "continue");
    expect(verify.delay == 2, "continue-d2");

    // Beating dead d0 scores higher than a shallow peer.
    slots[1].terminalDied = false;
    slots[1].autonomousAlive = true;
    slots[1].ticksSimulated = 12;
    slots[1].autonomousSurviveTicks = 12;
    slots[1].verifyGrants = 0;
    int const s1 = scoreCandidate(slots[1], slots[0], 64);
    int const s2 = scoreCandidate(slots[2], slots[0], 64);
    expect(s2 > 0 && s1 > 0, "scores-pos");
    expect(s2 > s1, "deeper-preferred");

    verifyIn.verifyLeft = 0;
    verifyIn.reserveLeft = 0;
    expect(choose(verifyIn).kind == ActionKind::StopUnknown, "unknown-empty");

    slots[2].ticksSimulated = 64;
    slots[2].terminalAliveCap = true;
    expect(choose(verifyIn).kind == ActionKind::FallThrough, "ft-at-horizon");

    if (fails == 0)
        report = "AdaptivePredictionScheduler OK";
    else
        report = fmt::format("AdaptivePredictionScheduler {} fails: {}", fails, report);
    return fails == 0;
}
