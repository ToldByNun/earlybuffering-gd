#ifndef EARLYBUFFER_CORE_CLICKINTENTGUARD_HPP
#define EARLYBUFFER_CORE_CLICKINTENTGUARD_HPP

#include "../config/StageConfig.hpp"
#include <string>

// Detects dense / rhythmic click spam (esp. wave corridors) so assist can Pass
// instead of buffering taps that have no real threat target.
// Orb/pad ahead or a nearby bake window → do not Pass (allow assist).
class ClickIntentGuard {
public:
    static ClickIntentGuard& get();

    void reset();

    // Record a real user Jump press (not inject) at attemptTick for player 0/1.
    void notePress(int playerSlot, int attemptTick, GameModeKind mode);

    // Density + interval regularity only (no orb/bake context).
    bool shouldPassThrough(int playerSlot, int attemptTick, GameModeKind mode) const;

    // Same as above, but nearOrbOrPad / nearBakeWindow force assist-allowed (false).
    bool shouldPassThrough(
        int playerSlot,
        int attemptTick,
        GameModeKind mode,
        bool nearOrbOrPad,
        bool nearBakeWindow
    ) const;

    // Pure evaluate for selfCheck / tests (sorted or unsorted ticks OK).
    static bool evaluateSpam(
        int const* ticks,
        int tickCount,
        int attemptTick,
        GameModeKind mode
    );

    static bool selfCheck(std::string& report);

    static int spamWindowTicks(GameModeKind mode);
    static int spamMinCount(GameModeKind mode);
    // Max mean gap (ticks) still counted as rhythmic spam.
    static float spamMaxMeanGap(GameModeKind mode);

private:
    ClickIntentGuard() = default;

    static constexpr int kRing = 12;

    struct Slot {
        int ticks[kRing]{};
        int count = 0;
        int next = 0;
    };

    Slot m_slots[2]{};
};

#endif // EARLYBUFFER_CORE_CLICKINTENTGUARD_HPP
