#ifndef SHADOW_REPLAY_RUNNER_HPP
#define SHADOW_REPLAY_RUNNER_HPP

#include "../../replay/replay_import.hpp"
#include "../planning/simulation_plan.hpp"
#include <vector>

#include "../shadow_simulator.hpp"

class EbShadowReplayRunner {
public:
    static EbShadowSimulator::ReplayResult replay(EbShadowSimulator& simulator, PlayLayer* layer,
                                                int slot, int startTick,
                                                std::vector<EbInputEvent> const& timeline,
                                                float deltaTime, int maxTicks, bool shiftActionBy,
                                                int actionShift);
    static EbShadowSimulator::BakeRunResult
    runBakeTimeline(EbShadowSimulator& simulator, PlayLayer* layer, int slot,
                    std::vector<EbReplayEvent> const& events, float deltaTime, int untilFrame,
                    int sampleFrame, bool applyP2ToShadow, bool traceFrames, bool parkReals,
                    int substeps, std::function<bool(int frame)> onFrame);
    static EbShadowSimulator::PathSample samplePath(EbShadowSimulator& simulator, PlayLayer* layer,
                                                  int slot, std::vector<EbReplayEvent> const& events,
                                                  float deltaTime, int untilFrame);

private:
    EbShadowReplayRunner() = delete;
};

#endif
