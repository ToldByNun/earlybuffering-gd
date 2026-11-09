#include "prefix_sim.hpp"
#include <algorithm>
#include <fmt/format.h>

bool EbPrefixSim::shouldSkipAfterPrefixDeath(int delay, int prefixDeathTick) {
    if (!(prefixDeathTick < 0)) {
        return delay > prefixDeathTick;
    }
    return false;
}

int EbPrefixSim::mergePrefixDeathTick(int current, int deathTick, bool diedBeforeAction) {
    if (!diedBeforeAction || deathTick < 0)
        return current;
    if (!(current < 0)) {
        return deathTick < current ? deathTick : current;
    }
    return deathTick;
}

int EbPrefixSim::updateSameCrashStreak(int streak, int prevDeathTick, int currDeathTick,
                                     bool currDied, bool diedBeforeAction) {
    // Prefix / pre-action deaths are handled by skip — not same-crash.
    if (!currDied || diedBeforeAction || currDeathTick < 0)
        return 0;
    if (prevDeathTick >= 0 && currDeathTick == prevDeathTick) {
        return streak + 1;
    }
    return 1;
}

bool EbPrefixSim::sameCrashShouldStop(int streak) {
    return streak >= EbPrefixSim::kSameCrashStreakStop;
}

bool EbPrefixSim::allowWaveTurnTheory(bool isWave) {
    return isWave;
}

bool EbPrefixSim::allowShipRecoverTheory(bool isShipLike) {
    return isShipLike;
}

bool EbPrefixSim::allowHoldBranchTheory(bool continuous, bool isRobot, bool isWave, bool isShipLike) {
    if (isRobot)
        return true;
    if (!continuous)
        return false;
    // Mode-gated: wave → WaveTurn path; ship-like → recover; other continuous OK.
    if (isWave)
        return EbPrefixSim::allowWaveTurnTheory(true);
    if (!(isShipLike)) {
        return true;
    }
    return EbPrefixSim::allowShipRecoverTheory(true);
}

std::vector<int> EbPrefixSim::rankViableByOutcome(QuickSample const* samples, int sampleCount) {
    std::vector<int> idx;
    if (!samples || sampleCount <= 0)
        return idx;
    for (int index = 0; index < sampleCount; ++index) {
        if (!samples[index].viable)
            continue;
        idx.push_back(index);
    }
    std::stable_sort(idx.begin(), idx.end(), [&](int firstValue, int secondValue) {
        auto const& sa = samples[firstValue];
        auto const& sb = samples[secondValue];
        if (sa.surviveTicks != sb.surviveTicks) {
            return sa.surviveTicks > sb.surviveTicks;
        }
        if (sa.endX != sb.endX)
            return sa.endX > sb.endX;
        return sa.delay < sb.delay;
    });
    std::vector<int> delays;
    delays.reserve(idx.size());
    for (int index : idx)
        delays.push_back(samples[index].delay);
    return delays;
}

std::vector<int> EbPrefixSim::delaysForFullScore(QuickSample const* samples, int sampleCount,
                                               int maxFull) {
    std::vector<int> out;
    if (!samples || sampleCount <= 0 || maxFull <= 0)
        return out;

    // Always keep viable survivors.
    for (int index = 0; index < sampleCount; ++index) {
        if (samples[index].viable)
            out.push_back(samples[index].delay);
    }

    // Among non-viable that acted then died: keep best endX for hold-branch salvage.
    std::vector<int> diedActed;
    for (int index = 0; index < sampleCount; ++index) {
        auto const& s = samples[index];
        if (s.viable || s.diedBeforeAction || !s.died)
            continue;
        diedActed.push_back(index);
    }
    std::stable_sort(diedActed.begin(), diedActed.end(), [&](int firstValue, int secondValue) {
        if (samples[firstValue].endX != samples[secondValue].endX) {
            return samples[firstValue].endX > samples[secondValue].endX;
        }
        return samples[firstValue].delay < samples[secondValue].delay;
    });

    for (int index : diedActed) {
        if (static_cast<int>(out.size()) >= maxFull)
            break;
        int const delayValue = samples[index].delay;
        if (std::find(out.begin(), out.end(), delayValue) == out.end()) {
            out.push_back(delayValue);
        }
    }

    if (static_cast<int>(out.size()) > maxFull) {
        out.resize(static_cast<size_t>(maxFull));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool EbPrefixSim::jitterAttemptAllowed(int attemptIndex, int topK) {
    if (!(topK <= 0)) {
        return attemptIndex >= 0 && attemptIndex < topK;
    }
    return false;
}

bool EbPrefixSim::selfCheck(std::string& report) {
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

    expect(!shouldSkipAfterPrefixDeath(3, -1), "no-prefix");
    expect(!shouldSkipAfterPrefixDeath(3, 5), "d-before-T");
    expect(shouldSkipAfterPrefixDeath(6, 5), "d-after-T");
    expect(shouldSkipAfterPrefixDeath(5, 5) == false, "d-eq-T");

    expect(mergePrefixDeathTick(-1, 4, true) == 4, "merge-first");
    expect(mergePrefixDeathTick(4, 2, true) == 2, "merge-earlier");
    expect(mergePrefixDeathTick(2, 9, true) == 2, "merge-keep");
    expect(mergePrefixDeathTick(2, 1, false) == 2, "merge-ignore");

    expect(updateSameCrashStreak(0, -1, 10, true, false) == 1, "crash-start");
    expect(updateSameCrashStreak(1, 10, 10, true, false) == 2, "crash-same");
    expect(sameCrashShouldStop(2), "crash-stop-2");
    expect(!sameCrashShouldStop(1), "crash-no-stop-1");
    expect(updateSameCrashStreak(2, 10, 12, true, false) == 1, "crash-reset");
    expect(updateSameCrashStreak(2, 10, 10, true, true) == 0, "crash-ignore-prefix");

    expect(allowWaveTurnTheory(true), "wave-on");
    expect(!allowWaveTurnTheory(false), "wave-off");
    expect(allowShipRecoverTheory(true), "ship-on");
    expect(!allowShipRecoverTheory(false), "ship-off");
    expect(allowHoldBranchTheory(true, false, true, false), "hold-wave");
    expect(allowHoldBranchTheory(true, false, false, true), "hold-ship");
    expect(allowHoldBranchTheory(false, true, false, false), "hold-robot");
    expect(!allowHoldBranchTheory(false, false, false, false), "hold-cube-off");

    expect(jitterAttemptAllowed(0), "jitter-0");
    expect(jitterAttemptAllowed(1), "jitter-1");
    expect(!jitterAttemptAllowed(2), "jitter-2-cap");
    expect(kJitterTopK == 2, "jitter-k");

    QuickSample samples[] = {
        {0, false, false, true, false, -1, 40, 100.f}, {1, true, false, false, false, 20, 20, 80.f},
        {2, true, false, false, false, 20, 20, 90.f},  {3, true, true, false, false, 3, 3, 50.f},
        {4, false, false, true, true, -1, 50, 120.f},
    };
    auto full = delaysForFullScore(samples, 5, 4);
    expect(std::find(full.begin(), full.end(), 0) != full.end(), "full-has-0");
    expect(std::find(full.begin(), full.end(), 4) != full.end(), "full-has-4");
    // Best died-acted by endX is delay 2 (90), then 1 (80); 3 is prefix-death skip.
    expect(std::find(full.begin(), full.end(), 2) != full.end(), "full-has-2");
    expect(std::find(full.begin(), full.end(), 3) == full.end(), "full-skip-prefix");

    // Same deathTick 20 on delays 1 and 2 → stop.
    int streak = 0;
    int prev = -1;
    streak = updateSameCrashStreak(streak, prev, 20, true, false);
    prev = 20;
    streak = updateSameCrashStreak(streak, prev, 20, true, false);
    expect(sameCrashShouldStop(streak), "fixture-same-crash");

    auto ranked = rankViableByOutcome(samples, 5);
    expect(!ranked.empty() && ranked[0] == 4, "rank-best-4");

    if (fails == 0) {
        report = "PrefixSim selfCheck OK";
        return true;
    }
    report = fmt::format("PrefixSim selfCheck {} fails: {}", fails, report);
    return false;
}
