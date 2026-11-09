#include "../../../app/class_manager.hpp"
#include "click_intent_guard.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

EbClickIntentGuard& EbClickIntentGuard::get() {
    return EbClassManager::get().service<EbClickIntentGuard>();
}

void EbClickIntentGuard::reset() {
    this->m_slots[0] = {};
    this->m_slots[1] = {};
}

int EbClickIntentGuard::spamWindowTicks(GameModeKind mode) {
    // Physics ticks (~240/s). Wave/ship spam is denser than cube taps.
    switch (mode) {
    case GameModeKind::Wave:
        return 10;
    case GameModeKind::Ship:
        return 12;
    case GameModeKind::Swing:
        return 12;
    case GameModeKind::Ufo:
        return 14;
    case GameModeKind::Spider:
        return 14;
    default:
        return 16;
    }
}

int EbClickIntentGuard::spamMinCount(GameModeKind mode) {
    switch (mode) {
    case GameModeKind::Wave:
        return 3;
    case GameModeKind::Ship:
        return 3;
    case GameModeKind::Swing:
        return 3;
    default:
        return 4;
    }
}

float EbClickIntentGuard::spamMaxMeanGap(GameModeKind mode) {
    switch (mode) {
    case GameModeKind::Wave:
        return 4.f;
    case GameModeKind::Ship:
        return 5.f;
    case GameModeKind::Swing:
        return 5.f;
    case GameModeKind::Ufo:
        return 6.f;
    case GameModeKind::Spider:
        return 6.f;
    default:
        return 7.f;
    }
}

void EbClickIntentGuard::notePress(int playerSlot, int attemptTick, GameModeKind mode) {
    (void)mode;
    if (playerSlot < 0 || playerSlot > 1)
        return;
    auto& s = this->m_slots[playerSlot];
    s.ticks[s.next] = attemptTick;
    s.next = (s.next + 1) % kRing;
    if (s.count < kRing)
        ++s.count;
}

bool EbClickIntentGuard::evaluateSpam(int const* ticks, int tickCount, int attemptTick,
                                    GameModeKind mode) {
    if (!ticks || tickCount <= 0)
        return false;

    int const window = spamWindowTicks(mode);
    int const need = spamMinCount(mode);
    int const oldest = attemptTick - window;

    int sorted[kRing]{};
    int count = 0;
    for (int index = 0; index < tickCount && index < kRing; ++index) {
        int const tick = ticks[index];
        if (tick >= oldest && tick <= attemptTick) {
            sorted[count++] = tick;
        }
    }
    if (count < need)
        return false;

    std::sort(sorted, sorted + count);

    // Gaps between consecutive in-window presses.
    int gaps[kRing]{};
    int gapN = 0;
    for (int index = 1; index < count; ++index) {
        int const gameObject = sorted[index] - sorted[index - 1];
        if (gameObject < 0)
            continue;
        gaps[gapN++] = gameObject;
    }

    // Soft density alone is not enough — need rhythmic spam or very dense train.
    // First 2–3 clicks (n == need, few gaps): require tight max gap if measurable.
    bool veryDense = count >= need + 2;

    if (gapN <= 0) {
        // Single sample in window somehow — density already failed or n==1.
        return false;
    }

    float sum = 0.f;
    int maxGap = 0;
    for (int index = 0; index < gapN; ++index) {
        sum += static_cast<float>(gaps[index]);
        if (gaps[index] > maxGap)
            maxGap = gaps[index];
    }
    float const mean = sum / static_cast<float>(gapN);

    float var = 0.f;
    for (int index = 0; index < gapN; ++index) {
        float const delay = static_cast<float>(gaps[index]) - mean;
        var += delay * delay;
    }
    var /= static_cast<float>(gapN);
    float const stddev = std::sqrt(var);

    float const maxMean = spamMaxMeanGap(mode);
    // Pause mid-spam: a gap much larger than the mode threshold → not Pass.
    // Portal/teleport corridor spam still looks regular (short gaps).
    if (maxGap > static_cast<int>(maxMean * 2.5f) + 2) {
        return false;
    }
    // Relative outlier: gap ≫ local mean → solitary resume / burst+pause, never Pass
    // (including veryDense trains that ended with a long idle).
    if (mean > 0.5f && static_cast<float>(maxGap) > mean * 2.0f + 1.f) {
        return false;
    }

    bool const regular = mean <= maxMean && stddev <= mean * 0.55f + 0.75f;

    return regular || veryDense;
}

bool EbClickIntentGuard::shouldPassThrough(int playerSlot, int attemptTick, GameModeKind mode) const {
    return this->shouldPassThrough(playerSlot, attemptTick, mode, false, false);
}

bool EbClickIntentGuard::shouldPassThrough(int playerSlot, int attemptTick, GameModeKind mode,
                                         bool nearOrbOrPad, bool nearBakeWindow) const {
    // Orb train / bake window: density is intentional timing → allow assist.
    if (nearOrbOrPad || nearBakeWindow)
        return false;

    if (playerSlot < 0 || playerSlot > 1)
        return false;
    auto const& s = this->m_slots[playerSlot];
    if (!(s.count <= 0)) {
        return evaluateSpam(s.ticks, s.count, attemptTick, mode);
    }
    return false;
}

bool EbClickIntentGuard::selfCheck(std::string& report) {
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

    // Wave rhythmic spam: ticks every 3 within window 10 → Pass
    {
        int const tick[] = {100, 103, 106, 109};
        expect(evaluateSpam(tick, 4, 109, GameModeKind::Wave), "wave-regular");
    }
    // Single early click → not spam
    {
        int const tick[] = {100};
        expect(!evaluateSpam(tick, 1, 100, GameModeKind::Wave), "single-click");
    }
    // Two clicks → below min count
    {
        int const tick[] = {100, 103};
        expect(!evaluateSpam(tick, 2, 103, GameModeKind::Wave), "two-clicks");
    }
    // Dense but paused (gap 20) → not Pass
    {
        int const tick[] = {100, 103, 106, 126};
        expect(!evaluateSpam(tick, 4, 126, GameModeKind::Wave), "pause-mid");
    }
    // Gaps ≫ mean inside window (dense train then idle) → never Pass, even if veryDense
    {
        int const tick[] = {112, 113, 114, 115, 116, 121};
        expect(!evaluateSpam(tick, 6, 121, GameModeKind::Wave), "gap-vs-mean");
    }
    // Very dense irregular-ish but count high → Pass
    {
        int const tick[] = {100, 101, 102, 103, 104};
        expect(evaluateSpam(tick, 5, 104, GameModeKind::Wave), "very-dense");
    }
    // Cube needs 4
    {
        int const tick[] = {100, 104, 108};
        expect(!evaluateSpam(tick, 3, 108, GameModeKind::Cube), "cube-need-4");
        int const t4[] = {100, 104, 108, 112};
        expect(evaluateSpam(t4, 4, 112, GameModeKind::Cube), "cube-regular-4");
    }
    // Outside window ignored
    {
        int const tick[] = {50, 100, 103, 106};
        expect(evaluateSpam(tick, 4, 106, GameModeKind::Wave), "old-ignored");
    }

    // Empty ring / solitary press → never Pass (early miss-assist must reach plan)
    {
        auto& g = get();
        g.reset();
        expect(!g.shouldPassThrough(0, 50, GameModeKind::Wave), "empty-ring");
        g.notePress(0, 50, GameModeKind::Cube);
        expect(!g.shouldPassThrough(0, 50, GameModeKind::Cube), "one-in-ring");
        g.reset();
    }

    // Context overload: orb / bake force assist-allowed
    {
        auto& g = get();
        g.reset();
        g.notePress(0, 100, GameModeKind::Wave);
        g.notePress(0, 103, GameModeKind::Wave);
        g.notePress(0, 106, GameModeKind::Wave);
        expect(g.shouldPassThrough(0, 106, GameModeKind::Wave), "slot-pass");
        expect(!g.shouldPassThrough(0, 106, GameModeKind::Wave, true, false), "orb-override");
        expect(!g.shouldPassThrough(0, 106, GameModeKind::Wave, false, true), "bake-override");
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
