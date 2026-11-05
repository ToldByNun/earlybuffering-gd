#ifndef EARLYBUFFER_CORE_ROUTEPATHBUILDER_HPP
#define EARLYBUFFER_CORE_ROUTEPATHBUILDER_HPP

#include "RoutePath.hpp"
#include "ShadowSimulator.hpp"

#include <string>
#include <vector>

// Builds RoutePath from bake validate frame traces (ModeAssist corridor halfs).
class RoutePathBuilder {
public:
    // Default stride: wave/ship/swing → 2; else → 4. Cap 4096 with stride×2.
    static int defaultStride(GameModeKind kind);

    // From BakeFrameTrace list (validate 1:1, full survival). Drops dead frames.
    // Empty / all-dead → empty path (caller must not write route.json).
    static RoutePath buildFromTraces(
        std::vector<ShadowSimulator::BakeFrameTrace> const& frames,
        bool parityOk = true
    );

    static bool selfCheck(std::string& report);

private:
    RoutePathBuilder() = delete;

    static float corridorHalf(GameModeKind kind);
    static GameModeKind modeFromTrace(int modeInt);
};

#endif // EARLYBUFFER_CORE_ROUTEPATHBUILDER_HPP
