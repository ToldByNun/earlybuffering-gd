#include "adaptive_decision_gate.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

EbAdaptiveDecisionGate::CandView EbAdaptiveDecisionGate::fromSlot(EbAdaptiveCandidateSlot const& slot) {
    CandView view;
    view.screened = slot.screened;
    view.autonomousAlive = slot.autonomousAlive;
    view.terminalDied = slot.terminalDied;
    view.terminalUnknown = slot.terminalUnknown;
    view.terminalAliveCap = slot.terminalAliveCap;
    view.zoneCleared = slot.zoneCleared;
    view.activatedRing = slot.activatedRing;
    view.recoverability = slot.recoverability;
    view.ticksSimulated = slot.ticksSimulated;
    view.autonomousSurviveTicks = slot.autonomousSurviveTicks;
    view.deathTick = slot.deathTick;
    view.endX = slot.endX;
    return view;
}

bool EbAdaptiveDecisionGate::isDelayWorthy(CandView const& c) {
    if (!c.screened)
        return false;
    if (c.terminalDied || c.terminalUnknown || !c.autonomousAlive)
        return false;
    // Recoverability alone never makes a dead/unknown path Delay-worthy.
    (void)c.recoverability;
    return true;
}

bool EbAdaptiveDecisionGate::isProvenSafe(CandView const& cand, int modelCap) {
    if (!isDelayWorthy(cand))
        return false;
    int const cap = std::max(1, modelCap);
    // Screen-quantum Alive ≠ safe. Horizon/cap or cleared threat (d0 Pass path).
    if (cand.ticksSimulated >= cap || cand.terminalAliveCap)
        return true;
    if (cand.zoneCleared)
        return true;
    return false;
}

bool EbAdaptiveDecisionGate::hasHazardProgress(CandView const& cand, CandView const& d0,
                                               int modelCap, int screenProofTicks) {
    if (!isDelayWorthy(cand))
        return false;
    int const cap = std::max(1, modelCap);
    // Cleared the clustered threat and remained Alive.
    if (cand.zoneCleared)
        return true;
    // Horizon wall alone is not progress — need past-d0 survival or zone clear.
    // Survived past the obstacle that killed d0 (still Alive — not a later soft death).
    if (d0.terminalDied && d0.deathTick >= 0 && cand.ticksSimulated > d0.deathTick + 1)
        return true;
    // Screen-Alive alone never counts while d0 is also Alive — that falsely
    // PassD0Safe'd presses (minProof==screen) and blocked classic fallthrough.
    // minProof only arms Delay when beating a dying d0.
    if (d0.terminalDied) {
        int const proofFloor = std::clamp(screenProofTicks, 8, 64);
        int const minProof = std::min(cap, proofFloor);
        if (cand.ticksSimulated >= minProof)
            return true;
    }
    return false;
}

bool EbAdaptiveDecisionGate::neighborsRobust(CandView const* cands, int limit, int robustK,
                                             int delay) {
    if (!cands || delay < 0 || delay > limit)
        return false;
    if (!isDelayWorthy(cands[static_cast<size_t>(delay)]))
        return false;
    int const offset = std::max(0, robustK);
    if (offset <= 0)
        return true;
    for (int entry = delay - offset; entry <= delay + offset; ++entry) {
        if (entry < 0 || entry > limit)
            continue;
        if (!isDelayWorthy(cands[static_cast<size_t>(entry)]))
            return false;
    }
    return true;
}

bool EbAdaptiveDecisionGate::rankBetter(CandView const& a, int delayA, CandView const& b, int delayB,
                                        float latencyWeight) {
    if (a.activatedRing != b.activatedRing)
        return a.activatedRing;
    float const lam = std::max(0.f, latencyWeight);
    float const scoreA = a.endX - lam * static_cast<float>(delayA);
    float const scoreB = b.endX - lam * static_cast<float>(delayB);
    if (std::fabs(scoreA - scoreB) > 0.5f)
        return scoreA > scoreB;
    return delayA < delayB;
}

EbAdaptiveDecisionGate::Result EbAdaptiveDecisionGate::decide(Input const& in) {
    Result out;
    out.action = PlanAction::Pass;
    out.reason = AssistReason::PassHopeless;
    out.delay = 0;

    if (!in.cands || in.limit < 0) {
        out.reason = AssistReason::PassInvalidArgs;
        return out;
    }

    CandView const& d0 = in.cands[0];
    int const cap = std::max(1, in.modelCap);
    int const proofTicks = in.screenProofTicks > 0 ? in.screenProofTicks : 24;

    // Incomplete verify with no Delay-progress / proven-safe d0 → PassBudget
    // (classic fallthrough). Screen-Alive d0 alone must not block fallthrough.
    if (in.budgetHitIncomplete) {
        bool anyProgress = false;
        if (isProvenSafe(d0, cap))
            anyProgress = true;
        for (int delay = 1; !anyProgress && delay <= in.limit; ++delay) {
            auto const& c = in.cands[static_cast<size_t>(delay)];
            if (hasHazardProgress(c, d0, cap, proofTicks))
                anyProgress = true;
        }
        if (!anyProgress) {
            out.reason = AssistReason::PassBudget;
            return out;
        }
    }

    // Collect gate-eligible delays. d0 only via proven-safe; later via hazard progress.
    int best = -1;
    bool bestRobust = false;
    for (int delay = 0; delay <= in.limit; ++delay) {
        auto const& c = in.cands[static_cast<size_t>(delay)];
        bool const eligible = (delay == 0) ? isProvenSafe(c, cap)
                                           : hasHazardProgress(c, d0, cap, proofTicks);
        if (!eligible)
            continue;
        bool const robust = neighborsRobust(in.cands, in.limit, in.robustK, delay);
        if (best < 0) {
            best = delay;
            bestRobust = robust;
            continue;
        }
        // Prefer ±k-robust when available.
        if (robust && !bestRobust) {
            best = delay;
            bestRobust = true;
            continue;
        }
        if (!robust && bestRobust)
            continue;
        if (rankBetter(c, delay, in.cands[static_cast<size_t>(best)], best, in.latencyWeight)) {
            best = delay;
            bestRobust = robust;
        }
    }

    if (best < 0) {
        // Alive but not horizon/zone-proven ≠ hopeless. With unlimited soft wall
        // Adaptive used to emit PassHopeless here and skip classic fallthrough,
        // killing Assist on every press. Treat as incomplete → PassBudget.
        if (isDelayWorthy(d0) && !d0.terminalDied) {
            out.reason = AssistReason::PassBudget;
            return out;
        }
        out.reason = in.budgetHitIncomplete ? AssistReason::PassBudget : AssistReason::PassHopeless;
        return out;
    }

    if (best == 0) {
        out.action = PlanAction::Pass;
        out.reason = AssistReason::PassD0Safe;
        out.delay = 0;
        out.nowIsSafe = true;
        out.threatCleared = true;
        out.jumpTick = 0;
        out.activatedRing = d0.activatedRing;
        return out;
    }

    auto const& win = in.cands[static_cast<size_t>(best)];
    out.action = PlanAction::Delay;
    out.reason = EbAssistVerdict::inferDelayReason(in.activatedRingPlan || win.activatedRing, false,
                                                   false);
    out.delay = best;
    out.nowIsSafe = false;
    out.threatCleared = win.zoneCleared;
    out.jumpTick = best;
    out.activatedRing = win.activatedRing;
    out.requireOrb = false;
    return out;
}

bool EbAdaptiveDecisionGate::selfCheck(std::string& report) {
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

    CandView cands[5]{};
    for (int delay = 0; delay <= 4; ++delay) {
        cands[delay].screened = true;
        cands[delay].terminalDied = true;
        cands[delay].deathTick = 5 + delay; // later deathTick alone
        cands[delay].autonomousSurviveTicks = 6 + delay;
        cands[delay].ticksSimulated = 6 + delay;
        cands[delay].endX = 50.f + static_cast<float>(delay);
    }
    // deathTick-dominated soft deaths must NOT arm Delay.
    Input deadIn;
    deadIn.cands = cands;
    deadIn.limit = 4;
    deadIn.modelCap = 64;
    auto const deadOut = decide(deadIn);
    expect(deadOut.action == PlanAction::Pass, "dead-pass");
    expect(deadOut.reason == AssistReason::PassHopeless, "dead-hopeless");
    expect(!hasHazardProgress(cands[4], cands[0], 64), "dead-no-progress");

    // d0 Alive at horizon → PassD0Safe.
    cands[0].terminalDied = false;
    cands[0].autonomousAlive = true;
    cands[0].ticksSimulated = 64;
    cands[0].terminalAliveCap = true;
    cands[0].autonomousSurviveTicks = 64;
    cands[0].endX = 200.f;
    Input d0In = deadIn;
    auto const d0Out = decide(d0In);
    expect(d0Out.action == PlanAction::Pass, "d0-pass");
    expect(d0Out.reason == AssistReason::PassD0Safe, "d0-safe");
    expect(d0Out.delay == 0, "d0-delay0");

    // Clear Alive delay past d0 death → Assist Delay.
    cands[0].terminalDied = true;
    cands[0].autonomousAlive = false;
    cands[0].terminalAliveCap = false;
    cands[0].deathTick = 8;
    cands[0].ticksSimulated = 9;
    cands[3].terminalDied = false;
    cands[3].autonomousAlive = true;
    cands[3].ticksSimulated = 40;
    cands[3].autonomousSurviveTicks = 40;
    cands[3].endX = 180.f;
    cands[3].zoneCleared = true;
    Input delayIn = deadIn;
    delayIn.robustK = 0;
    auto const delayOut = decide(delayIn);
    expect(delayOut.action == PlanAction::Delay, "delay-arm");
    expect(delayOut.delay == 3, "delay-3");
    expect(EbAssistVerdict::isDelay(delayOut.reason), "delay-reason");

    // Longer soft-dead surviveTicks must not beat an Alive hazard-progress cand.
    cands[4].terminalDied = true;
    cands[4].autonomousAlive = false;
    cands[4].deathTick = 50;
    cands[4].autonomousSurviveTicks = 51;
    cands[4].ticksSimulated = 51;
    cands[4].endX = 999.f;
    auto const noSoft = decide(delayIn);
    expect(noSoft.action == PlanAction::Delay && noSoft.delay == 3, "no-soft-death-win");

    // Recoverability alone on a dead ghost → still Pass.
    cands[3].autonomousAlive = false;
    cands[3].terminalDied = true;
    cands[3].recoverability = true;
    cands[3].zoneCleared = false;
    expect(!isDelayWorthy(cands[3]), "reco-not-worthy");
    auto const recoOut = decide(delayIn);
    expect(recoOut.action == PlanAction::Pass, "reco-pass");

    // Budget incomplete without progress → PassBudget.
    for (int delay = 0; delay <= 4; ++delay) {
        cands[delay].terminalDied = false;
        cands[delay].autonomousAlive = true;
        cands[delay].ticksSimulated = 10;
        cands[delay].terminalAliveCap = false;
        cands[delay].zoneCleared = false;
        cands[delay].recoverability = false;
        cands[delay].deathTick = -1;
    }
    Input budgetIn = deadIn;
    budgetIn.budgetHitIncomplete = true;
    auto const budgetOut = decide(budgetIn);
    expect(budgetOut.reason == AssistReason::PassBudget, "budget-unknown");

    // Rank ignores surviveTicks: earlier delay wins on equal endX.
    CandView a{};
    a.activatedRing = false;
    a.endX = 100.f;
    a.autonomousSurviveTicks = 10;
    CandView b = a;
    b.autonomousSurviveTicks = 40;
    b.endX = 100.f;
    expect(rankBetter(a, 1, b, 3, 0.25f), "rank-earlier-not-surv");

    // Screen-Alive while d0 also Alive is NOT hazard progress (false PassD0Safe).
    CandView proof{};
    proof.screened = true;
    proof.autonomousAlive = true;
    proof.ticksSimulated = 24;
    proof.endX = 120.f;
    CandView d0live = proof;
    d0live.ticksSimulated = 24;
    expect(!hasHazardProgress(proof, d0live, 96, 24), "no-min-proof-while-d0-alive");
    expect(!isProvenSafe(proof, 96), "screen-not-proven-safe");

    // minProof arms Delay only when beating a dying d0.
    CandView d0dead = d0live;
    d0dead.terminalDied = true;
    d0dead.autonomousAlive = false;
    d0dead.deathTick = 5;
    d0dead.ticksSimulated = 6;
    expect(hasHazardProgress(proof, d0dead, 96, 24), "min-proof-vs-dead-d0");
    expect(!hasHazardProgress(proof, d0dead, 96, 32), "min-proof-below-floor");

    // Screen-only Alive d0 → PassBudget (not PassD0Safe / not Hopeless), even
    // without budgetHitIncomplete — classic fallthrough must still run.
    for (int delay = 0; delay <= 4; ++delay) {
        cands[delay].terminalDied = false;
        cands[delay].autonomousAlive = true;
        cands[delay].ticksSimulated = 24;
        cands[delay].terminalAliveCap = false;
        cands[delay].zoneCleared = false;
        cands[delay].deathTick = -1;
    }
    Input screenSafeIn = deadIn;
    screenSafeIn.budgetHitIncomplete = false;
    screenSafeIn.screenProofTicks = 24;
    auto const screenSafeOut = decide(screenSafeIn);
    expect(screenSafeOut.reason == AssistReason::PassBudget, "screen-alive-budget");
    expect(screenSafeOut.action == PlanAction::Pass, "screen-alive-pass");

    screenSafeIn.budgetHitIncomplete = true;
    expect(decide(screenSafeIn).reason == AssistReason::PassBudget, "screen-alive-budget-hit");

    // --- Live session parity smokes (DevTrace regressions) ---

    // simUs=0: unproven Alive must never be Hopeless (blocked classic fallthrough).
    for (int delay = 0; delay <= 4; ++delay) {
        cands[delay] = CandView{};
        cands[delay].screened = true;
        cands[delay].autonomousAlive = true;
        cands[delay].ticksSimulated = 24;
        cands[delay].endX = 100.f + static_cast<float>(delay);
    }
    Input infiniteWallIn = deadIn;
    infiniteWallIn.budgetHitIncomplete = false;
    infiniteWallIn.screenProofTicks = 24;
    infiniteWallIn.modelCap = 84;
    auto const infiniteOut = decide(infiniteWallIn);
    expect(infiniteOut.reason == AssistReason::PassBudget, "infinite-wall-unproven-budget");
    expect(infiniteOut.reason != AssistReason::PassHopeless, "infinite-wall-not-hopeless");
    expect(infiniteOut.reason != AssistReason::PassD0Safe, "infinite-wall-not-d0safe");
    expect(infiniteOut.action == PlanAction::Pass, "infinite-wall-pass");

    // Hard click: dying d0 + Alive delay past deathTick → Delay (even without zone).
    for (int delay = 0; delay <= 4; ++delay) {
        cands[delay] = CandView{};
        cands[delay].screened = true;
        cands[delay].terminalDied = true;
        cands[delay].deathTick = 8;
        cands[delay].ticksSimulated = 9;
        cands[delay].endX = 50.f;
    }
    cands[2].terminalDied = false;
    cands[2].autonomousAlive = true;
    cands[2].deathTick = -1;
    cands[2].ticksSimulated = 24;
    cands[2].autonomousSurviveTicks = 24;
    cands[2].endX = 180.f;
    Input hardClickIn = deadIn;
    hardClickIn.robustK = 0;
    hardClickIn.screenProofTicks = 24;
    hardClickIn.modelCap = 84;
    auto const hardOut = decide(hardClickIn);
    expect(hardOut.action == PlanAction::Delay, "hard-click-delay");
    expect(hardOut.delay == 2, "hard-click-d2");
    expect(hasHazardProgress(cands[2], cands[0], 84, 24), "hard-click-progress");

    // Same-crash soft deaths (different deathTick inflation) → Hopeless, not Delay.
    for (int delay = 0; delay <= 4; ++delay) {
        cands[delay] = CandView{};
        cands[delay].screened = true;
        cands[delay].terminalDied = true;
        cands[delay].deathTick = 10 + delay;
        cands[delay].ticksSimulated = 11 + delay;
        cands[delay].autonomousSurviveTicks = 11 + delay;
        cands[delay].endX = 50.f + 10.f * static_cast<float>(delay);
    }
    Input softDeadIn = deadIn;
    softDeadIn.budgetHitIncomplete = false;
    auto const softDeadOut = decide(softDeadIn);
    expect(softDeadOut.action == PlanAction::Pass, "soft-dead-pass");
    expect(softDeadOut.reason == AssistReason::PassHopeless, "soft-dead-hopeless");

    // Zone-cleared Alive at short ticks still Delay-worthy vs dead d0.
    cands[0] = CandView{};
    cands[0].screened = true;
    cands[0].terminalDied = true;
    cands[0].deathTick = 4;
    cands[0].ticksSimulated = 5;
    cands[1] = CandView{};
    cands[1].screened = true;
    cands[1].autonomousAlive = true;
    cands[1].ticksSimulated = 12;
    cands[1].zoneCleared = true;
    cands[1].endX = 160.f;
    for (int delay = 2; delay <= 4; ++delay)
        cands[delay] = cands[0];
    Input zoneIn = deadIn;
    zoneIn.robustK = 0;
    auto const zoneOut = decide(zoneIn);
    expect(zoneOut.action == PlanAction::Delay && zoneOut.delay == 1, "zone-clear-delay");

    // Horizon-proven d0 beats later shallow Alive delays for PassD0Safe.
    cands[0] = CandView{};
    cands[0].screened = true;
    cands[0].autonomousAlive = true;
    cands[0].ticksSimulated = 84;
    cands[0].terminalAliveCap = true;
    cands[0].endX = 200.f;
    cands[3] = CandView{};
    cands[3].screened = true;
    cands[3].autonomousAlive = true;
    cands[3].ticksSimulated = 40;
    cands[3].zoneCleared = true;
    cands[3].endX = 220.f;
    for (int delay = 1; delay <= 4; ++delay) {
        if (delay == 3)
            continue;
        cands[delay] = CandView{};
        cands[delay].screened = true;
        cands[delay].terminalDied = true;
        cands[delay].deathTick = 5;
        cands[delay].ticksSimulated = 6;
    }
    Input provenD0In = deadIn;
    provenD0In.modelCap = 84;
    provenD0In.robustK = 0;
    // d0 alone at horizon (no later hazard-progress) → PassD0Safe.
    cands[3].terminalDied = true;
    cands[3].autonomousAlive = false;
    cands[3].zoneCleared = false;
    cands[3].ticksSimulated = 6;
    cands[3].deathTick = 5;
    expect(decide(provenD0In).reason == AssistReason::PassD0Safe, "proven-d0-alone");
    expect(decide(provenD0In).action == PlanAction::Pass, "proven-d0-pass");

    if (fails == 0)
        report = "AdaptiveDecisionGate OK";
    else
        report = fmt::format("AdaptiveDecisionGate {} fails: {}", fails, report);
    return fails == 0;
}
