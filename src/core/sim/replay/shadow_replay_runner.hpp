#ifndef SHADOW_REPLAY_RUNNER_HPP
#define SHADOW_REPLAY_RUNNER_HPP

#include "../../replay/replay_import.hpp"
#include "../planning/simulation_plan.hpp"
#include <vector>

#include "../shadow_simulator.hpp"

class ShadowReplayRunner {
public:
    static ShadowSimulator::ReplayResult replay(ShadowSimulator& simulator, PlayLayer* layer,
                                                int slot, int startTick,
                                                std::vector<InputEvent> const& timeline,
                                                float deltaTime, int maxTicks, bool shiftActionBy,
                                                int actionShift);
    static ShadowSimulator::BakeRunResult
    runBakeTimeline(ShadowSimulator& simulator, PlayLayer* layer, int slot,
                    std::vector<ReplayEvent> const& events, float deltaTime, int untilFrame,
                    int sampleFrame, bool applyP2ToShadow, bool traceFrames, bool parkReals,
                    int substeps, std::function<bool(int frame)> onFrame);
    static ShadowSimulator::PathSample samplePath(ShadowSimulator& simulator, PlayLayer* layer,
                                                  int slot, std::vector<ReplayEvent> const& events,
                                                  float deltaTime, int untilFrame);

private:
    ShadowReplayRunner() = delete;
};

#endif
