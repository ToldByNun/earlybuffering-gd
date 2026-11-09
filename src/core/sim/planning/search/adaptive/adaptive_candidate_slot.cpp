#include "adaptive_candidate_slot.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

void EbAdaptiveCandidateSlot::reset() {
    *this = EbAdaptiveCandidateSlot{};
}

void EbAdaptiveCandidateSlot::bindDelay(int delayTicks) {
    reset();
    delay = delayTicks;
}

bool EbAdaptiveCandidateSlot::hasCheckpoint() const {
    return checkpointValid && snapshotSlot >= 0 && ticksSimulated > 0 && !terminalDied &&
           !terminalUnknown;
}

void EbAdaptiveCandidateSlot::invalidateCheckpoint() {
    snapshotSlot = -1;
    checkpointValid = false;
    simAbsTickAtCheckpoint = -1;
}

void EbAdaptiveCandidateSlot::applyQuantumResult(EbShadowSimulator::RunResult const& result,
                                                 int snapshotSlotIndex, int ticksNow, int simAbsTick,
                                                 bool actedNow, bool tapReleasedNow,
                                                 bool holdingJumpNow, bool keepCheckpoint) {
    lastRaw = result;
    ticksSimulated = std::max(0, ticksNow);
    acted = actedNow;
    tapReleased = tapReleasedNow;
    holdingJump = holdingJumpNow;
    endX = result.endX;
    deathX = result.deathX;
    deathTick = result.deathTick;
    activatedRing = result.activatedRing;
    zoneCleared = result.zoneCleared;
    autonomousSurviveTicks =
        result.died && result.deathTick >= 0 ? result.deathTick + 1 : result.ticksRan;
    autonomousAlive = !result.died && !result.unknownTrunc && !result.unknown;
    terminalDied = result.died;
    terminalUnknown = result.unknown || result.unknownTrunc;
    terminalAliveCap = autonomousAlive && !keepCheckpoint && ticksSimulated > 0;

    if (keepCheckpoint && snapshotSlotIndex >= 0 && autonomousAlive && !terminalDied &&
        !terminalUnknown) {
        snapshotSlot = snapshotSlotIndex;
        simAbsTickAtCheckpoint = simAbsTick;
        checkpointValid = true;
    } else {
        invalidateCheckpoint();
    }
}

EbShadowSimulator::RunResult
EbAdaptiveCandidateSlot::mergeExtendResults(EbShadowSimulator::RunResult const& base,
                                            EbShadowSimulator::RunResult const& extension) {
    EbShadowSimulator::RunResult merged = base;
    int const baseTicks = std::max(0, base.ticksRan);
    int const extTicks = std::max(0, extension.ticksRan);
    merged.ticksRan = baseTicks + extTicks;
    merged.endX = extension.endX;
    merged.wasDashing = base.wasDashing || extension.wasDashing;
    merged.activatedRing = base.activatedRing || extension.activatedRing;
    merged.ringCount = std::max(base.ringCount, extension.ringCount);
    merged.zoneCleared = base.zoneCleared || extension.zoneCleared;
    if (base.zoneCleared)
        merged.ticksBeforeNeutral = base.ticksBeforeNeutral;
    else if (extension.zoneCleared && extension.ticksBeforeNeutral > 0)
        merged.ticksBeforeNeutral = baseTicks + extension.ticksBeforeNeutral;
    if (extension.jumped) {
        merged.jumped = true;
        merged.jumpTick = baseTicks + extension.jumpTick;
    } else if (base.jumped) {
        merged.jumped = true;
        merged.jumpTick = base.jumpTick;
    }
    if (extension.died) {
        merged.died = true;
        merged.deathTick = baseTicks + extension.deathTick;
        merged.deathX = extension.deathX;
        merged.deathObject = extension.deathObject;
        merged.diedBeforeAction = false;
    } else if (base.died) {
        merged.died = true;
    }
    if (extension.unknown || extension.unknownTrunc) {
        merged.unknown = extension.unknown;
        merged.unknownTrunc = extension.unknownTrunc;
        merged.unknownTick = baseTicks + std::max(0, extension.unknownTick);
        merged.toggleUnknown = extension.toggleUnknown;
    }
    if (extension.landedAfterAction)
        merged.landedAfterAction = true;
    if (merged.ticksBeforeNeutral <= 0)
        merged.ticksBeforeNeutral = merged.ticksRan;
    return merged;
}

bool EbAdaptiveCandidateSlot::selfCheck(std::string& report) {
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

    EbAdaptiveCandidateSlot slot;
    expect(!slot.hasCheckpoint(), "empty-no-cp");
    slot.bindDelay(3);
    expect(slot.delay == 3, "bind-delay");
    expect(!slot.hasCheckpoint(), "bind-no-cp");

    EbShadowSimulator::RunResult alive{};
    alive.ticksRan = 16;
    alive.endX = 100.f;
    slot.applyQuantumResult(alive, 2, 16, 50, true, false, true, true);
    expect(slot.hasCheckpoint(), "alive-cp");
    expect(slot.snapshotSlot == 2, "cp-slot");
    expect(slot.ticksSimulated == 16, "cp-ticks");
    expect(slot.autonomousSurviveTicks == 16, "auto-surv");
    expect(slot.autonomousAlive, "auto-alive");

    slot.invalidateCheckpoint();
    expect(!slot.hasCheckpoint(), "invalidated");
    expect(slot.ticksSimulated == 16, "metrics-kept");
    expect(slot.autonomousSurviveTicks == 16, "auto-kept");

    EbShadowSimulator::RunResult dead{};
    dead.ticksRan = 8;
    dead.died = true;
    dead.deathTick = 7;
    dead.endX = 40.f;
    dead.deathX = 40.f;
    slot.applyQuantumResult(dead, 3, 8, 20, true, false, false, true);
    expect(!slot.hasCheckpoint(), "dead-no-cp");
    expect(slot.terminalDied, "dead-term");
    expect(slot.autonomousSurviveTicks == 8, "dead-surv");

    // 16 + 16 contiguous ticks ≡ one 32-tick trajectory summary.
    EbShadowSimulator::RunResult base{};
    base.ticksRan = 16;
    base.endX = 80.f;
    base.jumped = true;
    base.jumpTick = 4;
    EbShadowSimulator::RunResult ext{};
    ext.ticksRan = 16;
    ext.endX = 160.f;
    ext.zoneCleared = true;
    ext.ticksBeforeNeutral = 10;
    auto const merged = mergeExtendResults(base, ext);
    expect(merged.ticksRan == 32, "merge-32");
    expect(std::fabs(merged.endX - 160.f) < 1e-3f, "merge-endx");
    expect(merged.jumped && merged.jumpTick == 4, "merge-jump");
    expect(merged.zoneCleared && merged.ticksBeforeNeutral == 16 + 10, "merge-zone");

    EbShadowSimulator::RunResult extDeath{};
    extDeath.ticksRan = 5;
    extDeath.died = true;
    extDeath.deathTick = 4;
    extDeath.endX = 90.f;
    extDeath.deathX = 90.f;
    auto const mergedDeath = mergeExtendResults(base, extDeath);
    expect(mergedDeath.ticksRan == 21, "merge-death-ticks");
    expect(mergedDeath.died && mergedDeath.deathTick == 20, "merge-death-tick");

    // Contiguous identity: merge(16,16).ticksRan == one-shot 32.
    EbShadowSimulator::RunResult oneShot{};
    oneShot.ticksRan = 32;
    oneShot.endX = 160.f;
    oneShot.jumped = true;
    oneShot.jumpTick = 4;
    oneShot.zoneCleared = true;
    oneShot.ticksBeforeNeutral = 26;
    expect(merged.ticksRan == oneShot.ticksRan, "16+16-eq-32-ticks");
    expect(std::fabs(merged.endX - oneShot.endX) < 1e-3f, "16+16-eq-32-endx");

    if (fails == 0)
        report = "AdaptiveCandidateSlot OK";
    else
        report = fmt::format("AdaptiveCandidateSlot {} fails: {}", fails, report);
    return fails == 0;
}
