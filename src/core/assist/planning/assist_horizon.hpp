#ifndef ASSIST_HORIZON_HPP
#define ASSIST_HORIZON_HPP

#include "../../../config/stage_config.hpp"
#include <string>

// Commit 15 — unified plan / score / traj draw horizons.
// Short ≈ today's modelFor autos; long only when SimTrust is OK (plan rule).
// Draw length = trust cap (trajDraw tracks scoreHorizon when trusted).
class EbAssistHorizon {
public:
    class Caps {
    public:
        int planCap = 48;      // runOne / baseline look-ahead after delay
        int scoreHorizon = 48; // planSequence / exit-predict
        int trajDraw = 56;     // TrajectoryVisualizer samplePath length

    private:
    };

    // settingsCap: StageConfig mode cap (>0 overrides auto base before mul).
    // lookAheadSetting: StageConfig::lookAheadCap() (>0 hard-clamps all three).
    static Caps resolve(GameModeKind mode, bool trustOk, int limit, float capMul,
                        int settingsCap = 0, int lookAheadSetting = 0);

    // Min survive ticks before an alive cand counts as Delay-success under trust-long.
    // Short trust keeps today's low bar (bin0 can still clear via threat).
    static int minSurviveTicksForSuccess(int scoreHorizon, bool trustOk);

    // Long path success: either survived long enough, or died but ModeSurvival-safe
    // (landing / recoverable / late-crash — e.g. platform after the jump).
    static bool longPathSuccessOk(int surviveTicks, bool died, bool survivalSafeForDelay,
                                  int minSurvive);

    static bool selfCheck(std::string& report);

private:
    EbAssistHorizon() = delete;

    static int applyMul(int base, float capMul);
    static Caps shortCaps(GameModeKind mode, int limit);
    static Caps longCaps(GameModeKind mode, int limit);
};

#endif
