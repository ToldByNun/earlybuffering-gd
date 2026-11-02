#ifndef EARLYBUFFER_CORE_TRAJDRAWHORIZON_HPP
#define EARLYBUFFER_CORE_TRAJDRAWHORIZON_HPP

#include <string>

// Draw-only HOLD/coast horizon — separate from AssistHorizon score/plan caps.
// Overlay samples until death (or hard max); planner / yellow preds stay short.
class TrajDrawHorizon {
public:
    // ≈4s @ 240 TPS. Not LongForesight's 320 planner bump.
    static constexpr int kDrawUntilDeathMax = 960;
    static constexpr int kRebuildPeriodShort = 16;
    static constexpr int kRebuildPeriodLong = 28;
    static constexpr int kLongPeriodThreshold = 96;

    // HOLD / coast sample length (until death or this cap).
    static int holdCoastDrawMax();

    // v2 yellow / dark-green pred length (scoreHorizon floor, never drawMax).
    static int predHorizon(int scoreHorizon);

    static int rebuildPeriod(int drawMax);

    // True when a path hit the hard max without dying (cap tick, not death X).
    static bool hitDrawCap(bool died, int pointsOrFrames, int drawMax);

    static bool selfCheck(std::string& report);

private:
    TrajDrawHorizon() = delete;
};

#endif // EARLYBUFFER_CORE_TRAJDRAWHORIZON_HPP
