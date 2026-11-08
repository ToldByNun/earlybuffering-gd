#ifndef EARLYBUFFER_CORE_LONGFORESIGHT_HPP
#define EARLYBUFFER_CORE_LONGFORESIGHT_HPP

#include <string>

// Live V2 only: long press-hold vs idle/coast foresight.
// Pure policy over path outcomes — ShadowSimulator fills PathStats
// (Trajectory sample loop or ShadowPlayer runOne/runBaseline).
class LongForesight {
public:
    static constexpr int kMinHorizon = 48;
    static constexpr int kExtendStep = 32;
    static constexpr int kDeathEdgeSlop = 4;
    static constexpr float kEndXSlop = 8.f;

    struct PathStats {
        bool died = false;
        int deathTick = -1;
        float endX = 0.f;
        int surviveTicks = 0;
    };

    struct Input {
        PathStats press{};
        PathStats idle{};
        int horizon = 0;
        // Second pass after extendHorizon (press died inside base window).
        bool hasExtended = false;
        PathStats pressExt{};
        PathStats idleExt{};
        int horizonExt = 0;
    };

    struct Result {
        bool pressSurvived = false;
        bool idleSurvived = false;
        bool idleSafer = false;
        bool pressBetter = false;
        // Press died and idle still lives → Delay search may find an escape.
        // Both-dead (even later idle deathTick) is not an escape signal.
        bool needDelaySearch = false;
        // Press and idle both die on the horizon used (no living coast).
        bool bothDead = false;
        bool extended = false;
        int horizonUsed = 0;
    };

    // Next horizon when press dies before the current end; 0 = no useful extend.
    static int extendHorizon(int currentHorizon, int pressDeathTick, int maxHorizon);

    // Idle looks safer than press (same idea as continuous classifyIdleSafer).
    static bool idleLooksSafer(PathStats const& press, PathStats const& idle);

    static Result decide(Input const& in);

    static bool selfCheck(std::string& report);

private:
    LongForesight() = default;
};

#endif // EARLYBUFFER_CORE_LONGFORESIGHT_HPP
