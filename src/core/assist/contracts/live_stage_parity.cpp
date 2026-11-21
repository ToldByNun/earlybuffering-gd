#include "live_stage_parity.hpp"
#include "../planning/perfect_assist_policy.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

int EbLiveStageParity::novaStageLeadFramesAll(NovaStage stage) {
    switch (stage) {
    case NovaStage::Off:
        return 0;
    case NovaStage::Stage1:
        return 1;
    case NovaStage::Stage2:
        return 2;
    case NovaStage::Stage3:
        return 3;
    case NovaStage::Extreme:
        return 8; // RE table float; UI "20 frames" is open mismatch
    case NovaStage::Custom:
        return -1;
    }
    return 0;
}

int EbLiveStageParity::novaStageLeadFramesWave(NovaStage stage) {
    switch (stage) {
    case NovaStage::Off:
        return 0;
    case NovaStage::Stage1:
        return 1;
    case NovaStage::Stage2:
        return 2;
    case NovaStage::Stage3:
        return 3;
    case NovaStage::Extreme:
        return 5;
    case NovaStage::Custom:
        return -1;
    }
    return 0;
}

int EbLiveStageParity::novaStageLeadFramesShip(NovaStage stage) {
    switch (stage) {
    case NovaStage::Off:
        return 0;
    case NovaStage::Stage1:
        return 1;
    case NovaStage::Stage2:
        return 1; // Stage 2/3 ship lead is 1 frame in Nova table
    case NovaStage::Stage3:
        return 1;
    case NovaStage::Extreme:
        return 5;
    case NovaStage::Custom:
        return -1;
    }
    return 0;
}

int EbLiveStageParity::framesToTicks(int frames) {
    if (frames <= 0)
        return 0;
    return frames * kTicksPerFrame;
}

int EbLiveStageParity::novaStageTicksAll(NovaStage stage) {
    int const frames = novaStageLeadFramesAll(stage);
    return frames < 0 ? -1 : framesToTicks(frames);
}

int EbLiveStageParity::novaStageTicksWave(NovaStage stage) {
    int const frames = novaStageLeadFramesWave(stage);
    return frames < 0 ? -1 : framesToTicks(frames);
}

int EbLiveStageParity::novaStageTicksShip(NovaStage stage) {
    int const frames = novaStageLeadFramesShip(stage);
    return frames < 0 ? -1 : framesToTicks(frames);
}

bool EbLiveStageParity::stageOffEquivalent(bool assistEnabled, int limitTicks) {
    return !assistEnabled || limitTicks <= 0;
}

EbLiveStageParity::DecisionKind EbLiveStageParity::classifyDecision(bool stageOff, int delayTicks) {
    if (stageOff)
        return DecisionKind::PassThrough;
    if (delayTicks > 0)
        return DecisionKind::Held;
    return DecisionKind::AsMade;
}

char const* EbLiveStageParity::decisionLabel(DecisionKind kind) {
    switch (kind) {
    case DecisionKind::PassThrough:
        return "pass-through";
    case DecisionKind::AsMade:
        return "in as made";
    case DecisionKind::Held:
        return "held";
    }
    return "pass-through";
}

bool EbLiveStageParity::windowLegal(int delay, int limit, int pressTick, int fireTick) {
    if (!(delay > 0 && limit > 0 && delay <= limit))
        return false;
    int const deadline = pressTick + limit;
    return fireTick >= pressTick && fireTick <= deadline;
}

bool EbLiveStageParity::delayArmVerified(bool candViable, bool budgetHit, bool unknown,
                                         bool softTrustPartial) {
    if (unknown)
        return false;
    return EbPerfectAssistPolicy::allowArmDelay(candViable, budgetHit, softTrustPartial);
}

bool EbLiveStageParity::midTickPhaseActive(float firePhase) {
    return std::isfinite(firePhase) && firePhase > 0.f && firePhase < 1.f;
}

bool EbLiveStageParity::midTickPrefixAllowed(float firePhase, int attemptTick, int fireTick) {
    return midTickPhaseActive(firePhase) && attemptTick == fireTick;
}

int EbLiveStageParity::shrinkLimitUnderBudget(int limit, int remUs, int fullBudgetUs) {
    if (limit <= 0)
        return 0;
    if (fullBudgetUs <= 0)
        return limit;
    if (remUs <= 0)
        return 0;
    if (remUs >= fullBudgetUs / 2)
        return limit;
    // Linear shrink below half-wall: remUs = half → keep limit; remUs→0 → 1.
    int const numer = limit * remUs * 2;
    int shrunk = numer / fullBudgetUs;
    if (shrunk < 1)
        shrunk = 1;
    return std::min(limit, shrunk);
}

bool EbLiveStageParity::selfCheck(std::string& report) {
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

    expect(kTicksPerFrame == 4, "tps-frame");
    expect(novaStageTicksAll(NovaStage::Off) == 0, "off-0");
    expect(novaStageTicksAll(NovaStage::Stage1) == 4, "s1-4");
    expect(novaStageTicksAll(NovaStage::Stage2) == 8, "s2-8");
    expect(novaStageTicksAll(NovaStage::Stage3) == 12, "s3-12");
    expect(novaStageTicksAll(NovaStage::Extreme) == 32, "extreme-all-32");
    expect(novaStageTicksWave(NovaStage::Extreme) == 20, "extreme-wave-20");
    expect(novaStageTicksShip(NovaStage::Extreme) == 20, "extreme-ship-20");
    expect(novaStageTicksShip(NovaStage::Stage2) == 4, "s2-ship-4");
    expect(novaStageTicksAll(NovaStage::Custom) == -1, "custom-open");

    expect(stageOffEquivalent(false, 8), "off-disabled");
    expect(stageOffEquivalent(true, 0), "off-limit0");
    expect(!stageOffEquivalent(true, 4), "on-limit4");

    expect(classifyDecision(true, 0) == DecisionKind::PassThrough, "class-pass");
    expect(classifyDecision(false, 0) == DecisionKind::AsMade, "class-as-made");
    expect(classifyDecision(false, 3) == DecisionKind::Held, "class-held");
    expect(std::string(decisionLabel(DecisionKind::Held)) == "held", "label-held");
    expect(std::string(decisionLabel(DecisionKind::AsMade)) == "in as made", "label-as-made");

    expect(windowLegal(3, 8, 100, 103), "win-ok");
    expect(windowLegal(8, 8, 100, 108), "win-edge");
    expect(!windowLegal(0, 8, 100, 100), "win-delay0");
    expect(!windowLegal(9, 8, 100, 109), "win-over");
    expect(!windowLegal(3, 8, 100, 99), "win-before");
    expect(!windowLegal(3, 8, 100, 109), "win-after");

    expect(delayArmVerified(true, false, false), "arm-ok");
    expect(delayArmVerified(true, true, false), "arm-budget-ok-cand");
    expect(!delayArmVerified(false, true, false), "arm-budget-no-cand");
    expect(!delayArmVerified(true, false, true), "arm-unknown");
    expect(!delayArmVerified(true, true, false, true), "arm-soft-budget");
    expect(delayArmVerified(true, false, false, true), "arm-soft-alone-ok");

    expect(midTickPhaseActive(0.5f), "mid-phase");
    expect(!midTickPhaseActive(0.f), "mid-phase0");
    expect(!midTickPhaseActive(1.f), "mid-phase1");
    expect(midTickPrefixAllowed(0.25f, 10, 10), "mid-prefix");
    expect(!midTickPrefixAllowed(0.25f, 9, 10), "mid-prefix-early");

    expect(shrinkLimitUnderBudget(16, 4000, 4000) == 16, "shrink-full");
    expect(shrinkLimitUnderBudget(16, 2000, 4000) == 16, "shrink-half");
    expect(shrinkLimitUnderBudget(16, 1000, 4000) == 8, "shrink-quarter");
    expect(shrinkLimitUnderBudget(16, 0, 4000) == 0, "shrink-zero-rem");
    expect(shrinkLimitUnderBudget(16, 1000, 0) == 16, "shrink-unlimited");

    if (fails == 0) {
        report = "LiveStageParity selfCheck OK";
        return true;
    }
    report = fmt::format("LiveStageParity selfCheck {} fails: {}", fails, report);
    return false;
}
