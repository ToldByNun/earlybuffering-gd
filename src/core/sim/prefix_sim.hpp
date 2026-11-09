#ifndef PREFIX_SIM_HPP
#define PREFIX_SIM_HPP

#include <cstddef>
#include <string>
#include <vector>

// Commit 10 — PrefixSim + two-pass / same-crash cut (ideas reimplemented; no upstream).
// Pure search policy: shared-prefix early-death skip, same-deathTick stop,
// mode-gated theories, two-pass full-score selection, jitter Top-2 only.
class EbPrefixSim {
public:
    static constexpr int kSameCrashStreakStop = 2;
    static constexpr int kJitterTopK = 2;
    static constexpr int kFullScoreTopK = 4;

    class QuickSample {
    public:
        int delay = 0;
        bool died = false;
        bool diedBeforeAction = false;
        bool viable = false;
        bool activatedRing = false;
        int deathTick = -1;
        int surviveTicks = 0;
        float endX = 0.f;

    private:
    };

    // Idle death at tick T before any press → every delay d > T shares that fate.
    static bool shouldSkipAfterPrefixDeath(int delay, int prefixDeathTick);

    // Update prefix death tick when a cand dies before action (keep earliest T).
    static int mergePrefixDeathTick(int current, int deathTick, bool diedBeforeAction);

    // Same deathTick after the press across delays → next obstacle, stop search.
    // streak counts consecutive equal post-action deathTicks; stop at >= kSameCrashStreakStop.
    static int updateSameCrashStreak(int streak, int prevDeathTick, int currDeathTick,
                                     bool currDied, bool diedBeforeAction);
    static bool sameCrashShouldStop(int streak);

    // Mode gates for extra theories (WaveTurn only on Wave, etc.).
    static bool allowWaveTurnTheory(bool isWave);
    static bool allowShipRecoverTheory(bool isShipLike);
    static bool allowHoldBranchTheory(bool continuous, bool isRobot, bool isWave, bool isShipLike);

    // Two-pass: after a quick skim of all delays, which need expensive full score
    // (hold-branch / deep re-sim)? Survivors + best died-but-acted by endX, capped.
    static std::vector<int> delaysForFullScore(QuickSample const* samples, int sampleCount,
                                               int maxFull = kFullScoreTopK);

    // Jitter (±3% firstDt) only on the first k ranked picks — never the whole pool.
    static bool jitterAttemptAllowed(int attemptIndex, int topK = kJitterTopK);

    // Rank viable delay indices by endX then surviveTicks (descending); stable by delay.
    static std::vector<int> rankViableByOutcome(QuickSample const* samples, int sampleCount);

    static bool selfCheck(std::string& report);

private:
    EbPrefixSim() = delete;
};

#endif
