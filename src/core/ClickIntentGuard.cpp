#include "ClickIntentGuard.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

ClickIntentGuard& ClickIntentGuard::get() {
    static ClickIntentGuard inst;
    return inst;
}

void ClickIntentGuard::reset() {
    this->m_slots[0] = {};
    this->m_slots[1] = {};
}

int ClickIntentGuard::spamWindowTicks(GameModeKind mode) {
    // Physics ticks (~240/s). Wave/ship spam is denser than cube taps.
    switch (mode) {
        case GameModeKind::Wave:  return 10;
        case GameModeKind::Ship:  return 12;
        case GameModeKind::Swing: return 12;
        case GameModeKind::Ufo:   return 14;
        case GameModeKind::Spider:return 14;
        default:                  return 16;
    }
}

int ClickIntentGuard::spamMinCount(GameModeKind mode) {
    switch (mode) {
        case GameModeKind::Wave:  return 3;
        case GameModeKind::Ship:  return 3;
        case GameModeKind::Swing: return 3;
        default:                  return 4;
    }
}

float ClickIntentGuard::spamMaxMeanGap(GameModeKind mode) {
    switch (mode) {
        case GameModeKind::Wave:  return 4.f;
        case GameModeKind::Ship:  return 5.f;
        case GameModeKind::Swing: return 5.f;
        case GameModeKind::Ufo:   return 6.f;
        case GameModeKind::Spider:return 6.f;
        default:                  return 7.f;
    }
}

void ClickIntentGuard::notePress(int playerSlot, int attemptTick, GameModeKind mode) {
    (void)mode;
    if (playerSlot < 0 || playerSlot > 1) return;
    auto& s = this->m_slots[playerSlot];
    s.ticks[s.next] = attemptTick;
    s.next = (s.next + 1) % kRing;
    if (s.count < kRing) ++s.count;
}

bool ClickIntentGuard::evaluateSpam(
    int const* ticks,
    int tickCount,
    int attemptTick,
    GameModeKind mode
) {
    if (!ticks || tickCount <= 0) return false;

    int const window = spamWindowTicks(mode);
    int const need = spamMinCount(mode);
    int const oldest = attemptTick - window;

    int sorted[kRing]{};
    int n = 0;
    for (int i = 0; i < tickCount && i < kRing; ++i) {
        int const t = ticks[i];
        if (t >= oldest && t <= attemptTick) {
            sorted[n++] = t;
        }
    }
    if (n < need) return false;

    std::sort(sorted, sorted + n);

    // Gaps between consecutive in-window presses.
    int gaps[kRing]{};
    int gapN = 0;
    for (int i = 1; i < n; ++i) {
        int const g = sorted[i] - sorted[i - 1];
        if (g < 0) continue;
        gaps[gapN++] = g;
    }

    // Soft density alone is not enough — need rhythmic spam or very dense train.
    // First 2–3 clicks (n == need, few gaps): require tight max gap if measurable.
    bool veryDense = n >= need + 2;

    if (gapN <= 0) {
        // Single sample in window somehow — density already failed or n==1.
        return false;
    }

    float sum = 0.f;
    int maxGap = 0;
    for (int i = 0; i < gapN; ++i) {
        sum += static_cast<float>(gaps[i]);
        if (gaps[i] > maxGap) maxGap = gaps[i];
    }
    float const mean = sum / static_cast<float>(gapN);

    float var = 0.f;
    for (int i = 0; i < gapN; ++i) {
        float const d = static_cast<float>(gaps[i]) - mean;
        var += d * d;
    }
    var /= static_cast<float>(gapN);
    float const stddev = std::sqrt(var);

    float const maxMean = spamMaxMeanGap(mode);
    // Pause mid-spam: a gap much larger than the rhythmic mean → not Pass.
    // Portal/teleport corridor spam still looks regular (short gaps).
    if (maxGap > static_cast<int>(maxMean * 2.5f) + 2) {
        return false;
    }

    bool const regular =
        mean <= maxMean
        && stddev <= mean * 0.55f + 0.75f;

    return regular || veryDense;
}

bool ClickIntentGuard::shouldPassThrough(
    int playerSlot, int attemptTick, GameModeKind mode
) const {
    return this->shouldPassThrough(playerSlot, attemptTick, mode, false, false);
}

bool ClickIntentGuard::shouldPassThrough(
    int playerSlot,
    int attemptTick,
    GameModeKind mode,
    bool nearOrbOrPad,
    bool nearBakeWindow
) const {
    // Orb train / bake window: density is intentional timing → allow assist.
    if (nearOrbOrPad || nearBakeWindow) return false;

    if (playerSlot < 0 || playerSlot > 1) return false;
    auto const& s = this->m_slots[playerSlot];
    if (s.count <= 0) return false;

    return evaluateSpam(s.ticks, s.count, attemptTick, mode);
}

bool ClickIntentGuard::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Wave rhythmic spam: ticks every 3 within window 10 → Pass
    {
        int const t[] = { 100, 103, 106, 109 };
        expect(evaluateSpam(t, 4, 109, GameModeKind::Wave), "wave-regular");
    }
    // Single early click → not spam
    {
        int const t[] = { 100 };
        expect(!evaluateSpam(t, 1, 100, GameModeKind::Wave), "single-click");
    }
    // Two clicks → below min count
    {
        int const t[] = { 100, 103 };
        expect(!evaluateSpam(t, 2, 103, GameModeKind::Wave), "two-clicks");
    }
    // Dense but paused (gap 20) → not Pass
    {
        int const t[] = { 100, 103, 106, 126 };
        expect(!evaluateSpam(t, 4, 126, GameModeKind::Wave), "pause-mid");
    }
    // Very dense irregular-ish but count high → Pass
    {
        int const t[] = { 100, 101, 102, 103, 104 };
        expect(evaluateSpam(t, 5, 104, GameModeKind::Wave), "very-dense");
    }
    // Cube needs 4
    {
        int const t[] = { 100, 104, 108 };
        expect(!evaluateSpam(t, 3, 108, GameModeKind::Cube), "cube-need-4");
        int const t4[] = { 100, 104, 108, 112 };
        expect(evaluateSpam(t4, 4, 112, GameModeKind::Cube), "cube-regular-4");
    }
    // Outside window ignored
    {
        int const t[] = { 50, 100, 103, 106 };
        expect(evaluateSpam(t, 4, 106, GameModeKind::Wave), "old-ignored");
    }

    // Context overload: orb / bake force assist-allowed
    {
        auto& g = get();
        g.reset();
        g.notePress(0, 100, GameModeKind::Wave);
        g.notePress(0, 103, GameModeKind::Wave);
        g.notePress(0, 106, GameModeKind::Wave);
        expect(g.shouldPassThrough(0, 106, GameModeKind::Wave), "slot-pass");
        expect(
            !g.shouldPassThrough(0, 106, GameModeKind::Wave, true, false),
            "orb-override"
        );
        expect(
            !g.shouldPassThrough(0, 106, GameModeKind::Wave, false, true),
            "bake-override"
        );
        g.reset();
    }

    // P2 independence
    {
        auto& g = get();
        g.reset();
        g.notePress(1, 200, GameModeKind::Ship);
        g.notePress(1, 204, GameModeKind::Ship);
        g.notePress(1, 208, GameModeKind::Ship);
        expect(g.shouldPassThrough(1, 208, GameModeKind::Ship), "p2-pass");
        expect(!g.shouldPassThrough(0, 208, GameModeKind::Ship), "p1-empty");
        g.reset();
    }

    if (fails == 0) {
        report = "ClickIntentGuard selfCheck OK";
        return true;
    }
    report = fmt::format("ClickIntentGuard {} fails: {}", fails, report);
    return false;
}
