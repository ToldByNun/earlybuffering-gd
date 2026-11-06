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

    // A normal route skips ghost fault frames. Pattern routes retain them, which
    // keeps an imported macro usable as a soft live-assist guide after a
    // simulator-only contact.
    static RoutePath buildFromTraces(
        std::vector<ShadowSimulator::BakeFrameTrace> const& frames,
        bool parityOk = true,
        bool keepFaultFrames = false
    );

    static bool selfCheck(std::string& report);

private:
    RoutePathBuilder() = delete;

    static float corridorHalf(GameModeKind kind);
    static GameModeKind modeFromTrace(int modeInt);
};

#endif // EARLYBUFFER_CORE_ROUTEPATHBUILDER_HPP
