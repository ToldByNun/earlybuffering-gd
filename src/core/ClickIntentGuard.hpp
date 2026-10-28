#ifndef EARLYBUFFER_CORE_CLICKINTENTGUARD_HPP
#define EARLYBUFFER_CORE_CLICKINTENTGUARD_HPP

#include "../config/StageConfig.hpp"

// Detects dense click spam (esp. wave) so assist can Pass instead of buffering
// taps that have no real threat target.
class ClickIntentGuard {
public:
    static ClickIntentGuard& get();

    void reset();

    // Record a real user Jump press (not inject) at attemptTick for player 0/1.
    void notePress(int playerSlot, int attemptTick, GameModeKind mode);

    // True → assist should Pass through (spam train). Caller may still assist
    // when an orb/pad is clearly nearby.
    bool shouldPassThrough(int playerSlot, int attemptTick, GameModeKind mode) const;

private:
    ClickIntentGuard() = default;

    static constexpr int kRing = 12;

    struct Slot {
        int ticks[kRing]{};
        int count = 0;
        int next = 0;
    };

    Slot m_slots[2]{};

    static int spamWindowTicks(GameModeKind mode);
    static int spamMinCount(GameModeKind mode);
};

#endif // EARLYBUFFER_CORE_CLICKINTENTGUARD_HPP
