#include "assist_timing_rules.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../dev/capture/dev_clock_contract.hpp"
#include "../assist_controller.hpp"
#include "../planning/orb_align.hpp"
#include <algorithm>
#include <string>

int AssistTimingRules::resolveEffectTick(int attemptTick, bool midStep, bool continuous) {
    // Single time basis: DevClockContract (Vertrag C). Live and eval share this.
    return DevClockContract::get().effectTick(attemptTick, midStep, continuous);
}

bool AssistTimingRules::effectTickSelfCheck(std::string& report) {
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

    // Discrete cube-like: mid-step stays on current tick; between frames advances.
    expect(AssistController::resolveEffectTick(10, true, false) == 10, "discrete-mid");
    expect(AssistController::resolveEffectTick(10, false, false) == 11, "discrete-between");

    // Continuous: never +1 (mid and between share the same effectTick).
    expect(AssistController::resolveEffectTick(10, true, true) == 10, "cont-mid");
    expect(AssistController::resolveEffectTick(10, false, true) == 10, "cont-between");
    expect(AssistController::resolveEffectTick(0, false, true) == 0, "cont-zero");
    expect(AssistController::resolveEffectTick(-3, false, false) == 1, "neg-clamp-discrete");
    expect(AssistController::resolveEffectTick(-3, true, true) == 0, "neg-clamp-cont");

    expect(AssistController::continuousModeKind(GameModeKind::Wave), "kind-wave");
    expect(AssistController::continuousModeKind(GameModeKind::Ship), "kind-ship");
    expect(AssistController::continuousModeKind(GameModeKind::Swing), "kind-swing");
    expect(AssistController::continuousModeKind(GameModeKind::Ufo), "kind-ufo");
    expect(AssistController::continuousModeKind(GameModeKind::Spider), "kind-spider");
    expect(!AssistController::continuousModeKind(GameModeKind::Cube), "kind-cube");
    expect(!AssistController::continuousModeKind(GameModeKind::Ball), "kind-ball");
    expect(!AssistController::continuousModeKind(GameModeKind::Robot), "kind-robot");

    // Mid-step must not double-advance: continuous mid == discrete mid == attemptTick.
    expect(AssistController::resolveEffectTick(42, true, true) ==
               AssistController::resolveEffectTick(42, true, false),
           "mid-no-double-advance");

    // Delegation parity with DevClockContract.
    expect(AssistController::resolveEffectTick(10, false, false) ==
               DevClockContract::get().effectTick(10, false, false),
           "delegate-discrete");
    expect(AssistController::resolveEffectTick(10, false, true) ==
               DevClockContract::get().effectTick(10, false, true),
           "delegate-cont");

    if (fails == 0) {
        report = "effectTickSelfCheck OK";
        return true;
    }
    report = fmt::format("effectTickSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistTimingRules::resolveFireTick(int effectTick, int delay, int jumpTick, bool requireOrb) {
    if (effectTick < 0)
        effectTick = 0;
    if (delay < 0)
        delay = 0;
    int fireDelay = delay;
    // Non-orb: sim may register the jump after the press tick. Injecting on
    // delay alone then feels one frame early vs when the jump matters.
    // Orb windows: activation timing (delay / dMin) wins — hold covers jump.
    if (!requireOrb && jumpTick > delay) {
        fireDelay = jumpTick;
    }
    return effectTick + fireDelay;
}

bool AssistTimingRules::shouldFireBuffered(int attemptTick, int fireTick, int pressTick,
                                           int planDelay, int limit) {
    if (attemptTick >= fireTick)
        return true;
    if (limit <= 0)
        return attemptTick >= fireTick;

    // Late-fire guard: last tick of the forgiveness window — inject rather
    // than miss entirely when jump alignment pushed fireTick to the edge.
    int const deadline = pressTick + limit;
    if (attemptTick >= deadline)
        return true;
    if (attemptTick >= deadline - 1 && attemptTick >= pressTick + std::max(0, planDelay)) {
        return true;
    }
    return false;
}

bool AssistTimingRules::fireTickSelfCheck(std::string& report) {
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

    // Same-tick jump → fire at delay.
    expect(AssistController::resolveFireTick(100, 3, 3, false) == 103, "jump-eq-delay");
    // Jump one tick later → fire at jumpTick (non-orb).
    expect(AssistController::resolveFireTick(100, 3, 4, false) == 104, "jump-after-delay");
    // Orb keeps activation delay even if jump lags.
    expect(AssistController::resolveFireTick(100, 3, 5, true) == 103, "orb-keeps-delay");
    expect(AssistController::resolveFireTick(0, 0, 0, false) == 0, "zero");
    expect(AssistController::resolveFireTick(-2, 2, 2, false) == 2, "neg-effect");

    expect(AssistController::shouldFireBuffered(10, 10, 5, 3, 8), "due-eq");
    expect(AssistController::shouldFireBuffered(11, 10, 5, 3, 8), "due-late");
    expect(!AssistController::shouldFireBuffered(9, 10, 5, 3, 8), "not-yet");
    // Last tick of window (press=5, limit=8 → deadline=13): at 12 with planDelay met.
    expect(AssistController::shouldFireBuffered(12, 14, 5, 3, 8), "last-chance");
    expect(!AssistController::shouldFireBuffered(11, 14, 5, 3, 8), "not-last-chance");
    expect(AssistController::shouldFireBuffered(13, 14, 5, 3, 8), "at-deadline");

    // Non-orb Delay must keep planned fire even when incidental act band is early.
    // (Mirrors planPress: only requireOrb may clamp to dMin/dMax.)
    {
        int const effect = 100;
        int const delay = 4;
        int const jump = 4;
        int fire = AssistController::resolveFireTick(effect, delay, jump, false);
        expect(fire == 104, "non-orb-fire-at-delay");
        // Incidental orb band [0,1] must NOT collapse fire when !requireOrb.
        int const dMax = 1;
        int collapsed = std::min(fire, effect + dMax);
        expect(collapsed == 101, "demo-bad-clamp");
        expect(fire == 104, "non-orb-ignores-incidental-dmax");
    }

    if (fails == 0) {
        report = "fireTickSelfCheck OK";
        return true;
    }
    report = fmt::format("fireTickSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistTimingRules::resolveQueuedHoldTicks(bool requireHold, bool continuous, int releaseAfter,
                                              int releaseEarliest, int releaseLatest,
                                              int extendCap) {
    if (extendCap < 0)
        extendCap = 0;

    int hold = -1;
    if (continuous) {
        // Earliest-safe only — never drag ceiling with blind +limit.
        if (releaseEarliest >= 0)
            hold = releaseEarliest;
        else if (releaseAfter >= 0)
            hold = releaseAfter;
        else
            return 0;
    } else if (releaseAfter >= 0) {
        hold = releaseAfter;
    } else if (requireHold) {
        hold = extendCap;
    } else {
        return 0;
    }

    if (hold <= 0)
        return 0;
    if (releaseLatest >= 0)
        hold = std::min(hold, releaseLatest);
    hold = std::min(hold, extendCap);
    return std::max(0, hold);
}

bool AssistTimingRules::queuedHoldSelfCheck(std::string& report) {
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

    // Plain Delay tap + early release → ASAP (0 hold).
    expect(AssistController::resolveQueuedHoldTicks(false, false, -1, -1, -1, 8) == 0,
           "plain-asap");
    expect(AssistController::resolveQueuedHoldTicks(false, false, 0, -1, -1, 8) == 0, "rel0-asap");

    // Planner releaseAfter honored (discrete).
    expect(AssistController::resolveQueuedHoldTicks(false, false, 5, 2, 10, 8) == 5, "rel-after");
    expect(AssistController::resolveQueuedHoldTicks(true, false, 5, 2, 10, 8) == 5,
           "hold-rel-after");

    // requireHold without release fields → clamp to extendCap (dash/robot).
    expect(AssistController::resolveQueuedHoldTicks(true, false, -1, -1, -1, 8) == 8,
           "hold-fallback-cap");

    // Continuous: earliest-safe only; no blind +limit.
    expect(AssistController::resolveQueuedHoldTicks(true, true, 10, 2, 14, 16) == 2,
           "cont-earliest");
    expect(AssistController::resolveQueuedHoldTicks(true, true, 6, -1, 14, 16) == 6,
           "cont-rel-after");
    expect(AssistController::resolveQueuedHoldTicks(true, true, -1, -1, -1, 16) == 0,
           "cont-no-blind-cap");

    // Clamp to latest and extendCap.
    expect(AssistController::resolveQueuedHoldTicks(false, false, 12, 2, 4, 16) == 4,
           "clamp-latest");
    expect(AssistController::resolveQueuedHoldTicks(false, false, 12, 2, 20, 8) == 8, "clamp-cap");

    if (fails == 0) {
        report = "queuedHoldSelfCheck OK";
        return true;
    }
    report = fmt::format("queuedHoldSelfCheck {} fails: {}", fails, report);
    return false;
}

int AssistTimingRules::acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire,
                                          int absMax, int robustK, int deadline, bool requireOrb,
                                          bool planOk) {
    (void)requireOrb; // Commit 13: same ≤+1 / never-earlier for orb + non-orb
    return OrbAlign::acceptRecheckSlide(oldFire, attemptTick, proposedFire, absMax, robustK,
                                        deadline, planOk);
}

bool AssistTimingRules::recheckSlideSelfCheck(std::string& report) {
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

    expect(AssistController::acceptRecheckSlide(10, 8, 12, 20, 1, 20, false, false) < 0,
           "reject-bad-plan");
    // Commit 13: late slide capped at +1
    expect(AssistController::acceptRecheckSlide(10, 8, 20, 30, 0, 30, false, true) == 11,
           "cap-slide-plus1");
    expect(AssistController::acceptRecheckSlide(15, 8, 11, 30, 0, 30, false, true) < 0,
           "early-slide-reject");
    // robustK: absMax 20, k=2 → safeMax 18, then +1 cap from oldFire=10 → 11
    expect(AssistController::acceptRecheckSlide(10, 8, 19, 20, 2, 30, false, true) == 11,
           "robust-then-cap");
    expect(AssistController::acceptRecheckSlide(10, 12, 12, 20, 0, 20, false, true) < 0,
           "not-future");
    // Orb/toggle: same ≤+1 / never earlier (dMin wait is a separate path)
    expect(AssistController::acceptRecheckSlide(10, 8, 18, 20, 0, 20, true, true) == 11,
           "orb-slide-plus1");
    expect(AssistController::acceptRecheckSlide(15, 8, 11, 30, 0, 30, true, true) < 0,
           "orb-no-earlier");
    expect(AssistController::acceptRecheckSlide(12, 8, 12, 20, 0, 20, false, true) < 0,
           "noop-reject");
    expect(AssistController::acceptRecheckSlide(10, 8, 14, 30, 0, 8, false, true) < 0,
           "deadline-collapse");
    expect(AssistController::acceptRecheckSlide(10, 8, 14, 30, 0, 13, false, true) == 11,
           "deadline-cap-slide");

    if (fails == 0) {
        report = "recheckSlideSelfCheck OK";
        return true;
    }
    report = fmt::format("recheckSlideSelfCheck {} fails: {}", fails, report);
    return false;
}
