#include "mode_survival.hpp"
#include "perfect_assist_policy.hpp"
#include <cmath>
#include <cstring>
#include <fmt/format.h>

char const* ModeSurvival::classId(Class candidate) {
    switch (candidate) {
    case Class::Alive:
        return "alive";
    case Class::Landing:
        return "landing";
    case Class::Recoverable:
        return "recoverable";
    case Class::LateCrashAfterClear:
        return "late-crash";
    case Class::Lethal:
        return "lethal";
    }
    return "lethal";
}

bool ModeSurvival::countsAsSafeForDelay(Class candidate) {
    return candidate == Class::Alive || candidate == Class::Landing ||
           candidate == Class::Recoverable || candidate == Class::LateCrashAfterClear;
}

bool ModeSurvival::countsAsDelayWorthy(Class candidate) {
    return candidate == Class::Alive;
}

bool ModeSurvival::shouldSkipFalseOpportunity(Class candidate) {
    return candidate == Class::LateCrashAfterClear || candidate == Class::Landing;
}

bool ModeSurvival::narrowOverlapIgnore(Input const& in) {
    // Narrow only — never ignore a fresh overlapping spike as "already there".
    return in.hazardOverlapAtClick && in.hazardIsFollowOrSpent;
}

float ModeSurvival::clearMarginPx(float obstacleX, float endX) {
    return endX - obstacleX;
}

bool ModeSurvival::waveSmallestClearOk(float clearPx, float marginPx) {
    // Commit 11: same +1px margin rule as PerfectAssistPolicy wave helper.
    return PerfectAssistPolicy::preferSmallerWaveClear(clearPx, clearPx, marginPx);
}

bool ModeSurvival::waveFlipProbeClears(float obstacleX, float const sampleEndX[kWaveFlipProbes],
                                       float marginPx) {
    if (!sampleEndX)
        return false;
    for (int index = 0; index < kWaveFlipProbes; ++index) {
        if (ModeSurvival::waveSmallestClearOk(
                ModeSurvival::clearMarginPx(obstacleX, sampleEndX[index]), marginPx)) {
            return true;
        }
    }
    return false;
}

bool ModeSurvival::ascentAloneNotLanding(bool died, bool zoneCleared, bool groundedSettled,
                                         bool deathLooksLikeAscent) {
    // ParityGate: ceiling-bump shaped death without settle/clear ≠ Landing.
    if (!died)
        return true;
    if (!(deathLooksLikeAscent && !zoneCleared && !groundedSettled)) {
        return false;
    }
    return true;
}

ModeSurvival::Class ModeSurvival::classify(Input const& in) {
    if (in.diedBeforeAction)
        return Class::Lethal;

    if (ModeSurvival::narrowOverlapIgnore(in)) {
        // Spent/follow hazard already eating the player — not this click's fault.
        if (!in.died)
            return Class::Alive;
        return Class::LateCrashAfterClear;
    }

    if (!in.died)
        return Class::Alive;

    bool const pastZone = in.zoneValid && in.deathX > in.zoneEndX;
    bool const cleared =
        in.zoneCleared || (in.groundedSettled && in.ticksBeforeNeutral > 0) || pastZone;

    // Cube/Ball/Robot: landing past threat then later death → not Delay-lethal.
    if (in.isCubeLike && !in.continuous) {
        if (cleared) {
            if (in.groundedSettled || in.zoneCleared)
                return Class::Landing;
            return Class::LateCrashAfterClear;
        }
        return Class::Lethal;
    }

    // Wave: recoverable after clear / past zone (turn-away). No clear → lethal.
    // Ascent-shaped death without clear must stay Lethal (ceiling-bump ParityGate).
    if (in.isWave || (in.continuous && !in.isShipLike)) {
        if (cleared)
            return Class::Recoverable;
        return Class::Lethal;
    }

    // Ship / UFO / Spider / Swing: recover after clear; else lethal.
    if (in.isShipLike || in.continuous) {
        if (cleared)
            return Class::Recoverable;
        return Class::Lethal;
    }

    // Fallback discrete: only safe if cleared.
    if (!(cleared)) {
        return Class::Lethal;
    }
    return Class::LateCrashAfterClear;
}

bool ModeSurvival::selfCheck(std::string& report) {
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

    expect(countsAsSafeForDelay(Class::Alive), "safe-alive");
    expect(countsAsSafeForDelay(Class::Landing), "safe-landing");
    expect(countsAsSafeForDelay(Class::Recoverable), "safe-recoverable");
    expect(countsAsSafeForDelay(Class::LateCrashAfterClear), "safe-late");
    expect(!countsAsSafeForDelay(Class::Lethal), "lethal-unsafe");
    expect(countsAsDelayWorthy(Class::Alive), "worthy-alive");
    expect(!countsAsDelayWorthy(Class::Landing), "worthy-not-landing");
    expect(!countsAsDelayWorthy(Class::Recoverable), "worthy-not-recover");
    expect(!countsAsDelayWorthy(Class::LateCrashAfterClear), "worthy-not-late");
    expect(!countsAsDelayWorthy(Class::Lethal), "worthy-not-lethal");
    expect(shouldSkipFalseOpportunity(Class::Landing), "skip-landing");
    expect(shouldSkipFalseOpportunity(Class::LateCrashAfterClear), "skip-late");
    expect(!shouldSkipFalseOpportunity(Class::Lethal), "no-skip-lethal");
    expect(!shouldSkipFalseOpportunity(Class::Recoverable), "no-skip-recover");
    expect(!shouldSkipFalseOpportunity(Class::Alive), "no-skip-alive");

    // Cube: zoneCleared + death past end → Landing / soft-safe, not Delay-worthy.
    {
        Input input{};
        input.isCubeLike = true;
        input.died = true;
        input.zoneValid = true;
        input.zoneCleared = true;
        input.groundedSettled = true;
        input.ticksBeforeNeutral = 10;
        input.deathX = 220.f;
        input.zoneEndX = 200.f;
        auto const candidate = classify(input);
        expect(candidate == Class::Landing, "cube-landing");
        expect(countsAsSafeForDelay(candidate), "cube-landing-soft-safe");
        expect(!countsAsDelayWorthy(candidate), "cube-landing-not-worthy");
        expect(shouldSkipFalseOpportunity(candidate), "cube-landing-skip-opp");
    }

    // Cube: die inside zone, no settle → Lethal.
    {
        Input input{};
        input.isCubeLike = true;
        input.died = true;
        input.zoneValid = true;
        input.deathX = 150.f;
        input.zoneEndX = 200.f;
        expect(classify(input) == Class::Lethal, "cube-in-zone-lethal");
    }

    // Overlap-ignore too broad must fail: overlap without follow/spent → Lethal.
    {
        Input input{};
        input.isCubeLike = true;
        input.died = true;
        input.hazardOverlapAtClick = true;
        input.hazardIsFollowOrSpent = false;
        input.zoneValid = true;
        input.deathX = 150.f;
        input.zoneEndX = 200.f;
        expect(!narrowOverlapIgnore(input), "overlap-not-narrow");
        expect(classify(input) == Class::Lethal, "overlap-still-lethal");
    }

    // Narrow overlap ignore (follow/spent) → LateCrashAfterClear.
    {
        Input input{};
        input.died = true;
        input.hazardOverlapAtClick = true;
        input.hazardIsFollowOrSpent = true;
        expect(narrowOverlapIgnore(input), "overlap-narrow");
        expect(classify(input) == Class::LateCrashAfterClear, "overlap-late");
    }

    // Wave recoverable after clear.
    {
        Input input{};
        input.continuous = true;
        input.isWave = true;
        input.died = true;
        input.zoneValid = true;
        input.zoneCleared = true;
        input.deathX = 260.f;
        input.zoneEndX = 200.f;
        expect(classify(input) == Class::Recoverable, "wave-recoverable");
    }

    // Wave ceiling-bump without clear → Lethal (ParityGate).
    {
        Input input{};
        input.continuous = true;
        input.isWave = true;
        input.died = true;
        input.zoneValid = true;
        input.deathX = 120.f;
        input.zoneEndX = 200.f;
        expect(classify(input) == Class::Lethal, "wave-ceiling-lethal");
        expect(ascentAloneNotLanding(true, false, false, true), "ascent-alone-not-landing");
    }

    // Ship-like recover after past-zone death.
    {
        Input input{};
        input.continuous = true;
        input.isShipLike = true;
        input.died = true;
        input.zoneValid = true;
        input.deathX = 250.f;
        input.zoneEndX = 200.f;
        expect(classify(input) == Class::Recoverable, "ship-recover");
    }

    // Alive.
    {
        Input input{};
        input.isCubeLike = true;
        input.died = false;
        expect(classify(input) == Class::Alive, "alive");
    }

    // Wave flip probes: smallest clear +1 margin.
    {
        float ends[3] = {100.f, 201.f, 150.f};
        expect(waveFlipProbeClears(200.f, ends, 0.f), "wave-flip-clears");
        float miss[3] = {199.f, 198.f, 197.f};
        expect(!waveFlipProbeClears(200.f, miss, 0.f), "wave-flip-miss");
        expect(waveSmallestClearOk(2.f, 1.f), "wave-clear-ok");
        expect(!waveSmallestClearOk(1.f, 1.f), "wave-clear-tight");
        expect(kWaveFlipProbes == 3, "wave-flip-n");
    }

    expect(std::strcmp(classId(Class::Landing), "landing") == 0, "id-landing");

    if (fails == 0) {
        report = "ModeSurvival selfCheck OK";
        return true;
    }
    report = fmt::format("ModeSurvival selfCheck {} fails: {}", fails, report);
    return false;
}
