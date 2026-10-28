#include "ClickIntentGuard.hpp"

ClickIntentGuard& ClickIntentGuard::get() {
    static ClickIntentGuard inst;
    return inst;
}

void ClickIntentGuard::reset() {
    m_slots[0] = {};
    m_slots[1] = {};
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

void ClickIntentGuard::notePress(int playerSlot, int attemptTick, GameModeKind mode) {
    (void)mode;
    if (playerSlot < 0 || playerSlot > 1) return;
    auto& s = m_slots[playerSlot];
    s.ticks[s.next] = attemptTick;
    s.next = (s.next + 1) % kRing;
    if (s.count < kRing) ++s.count;
}

bool ClickIntentGuard::shouldPassThrough(
    int playerSlot, int attemptTick, GameModeKind mode
) const {
    if (playerSlot < 0 || playerSlot > 1) return false;
    auto const& s = m_slots[playerSlot];
    if (s.count <= 0) return false;

    int const window = spamWindowTicks(mode);
    int const need = spamMinCount(mode);
    int const oldest = attemptTick - window;

    int inWindow = 0;
    for (int i = 0; i < s.count; ++i) {
        int const t = s.ticks[i];
        // Include the current press if already noted, and any recent ones.
        if (t >= oldest && t <= attemptTick) ++inWindow;
    }
    return inWindow >= need;
}
