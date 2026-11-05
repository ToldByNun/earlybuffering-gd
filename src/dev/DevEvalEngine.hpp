#ifndef DEV_EVAL_ENGINE_HPP
#define DEV_EVAL_ENGINE_HPP

#include "DevClockContract.hpp"
#include "DevConfig.hpp"
#include "DevEvalRestore.hpp"
#include "DevLiveWitness.hpp"
#include "DevObjective.hpp"
#include "DevStateDiff.hpp"
#include "../core/ShadowSimulator.hpp"

#include <cstdint>
#include <string>
#include <vector>

class PlayLayer;

// Clock-bound eval + grid/tail/objectives.
class DevEvalEngine {
public:
    struct Report {
        std::string timeBasis;
        int clockSchema = 0;
        std::string liveSimPathId;
        std::string evalSimPathId;
        uint64_t stepScheduleHash = 0;
        bool clockMismatch = false;
        DevClassifyResult fidelity{};
        DevEvalRestore restore{};
        DevObjective::Sample sample{};
        bool ran = false;
    };

    struct GridReport {
        std::string timeBasis;
        int clockSchema = 0;
        std::string liveSimPathId;
        std::string evalSimPathId;
        uint64_t stepScheduleHash = 0;
        bool clockMismatch = false;
        DevClassifyResult fidelity{};
        DevEvalRestore restore{};
        std::vector<DevObjective::Sample> samples;
        DevObjective::Result objective{};
        bool objectivePartial = false;
        DevConfig::EvalGrid grid = DevConfig::EvalGrid::Band;
        int horizonTicks = 48;
        int tailTicks = 48;
        int limit = 0;
    };

    // Counterfactual delay on the witness stamp (evalDelayFromFreeze).
    // Pass horizonTicks = H (primary only) or H+T (combined; use classifyPrimaryAndTail).
    static Report evalDelay(
        PlayLayer* layer,
        DevLiveWitness const& witness,
        int delay,
        int horizonTicks,
        bool down,
        DevClassifyOptions const& opts,
        int* tickBudgetLeft = nullptr
    );

    // Replay absolute actualFireTick on the same stamp.
    static Report replayActualFire(
        PlayLayer* layer,
        DevLiveWitness const& witness,
        int actualFireTickAbs,
        int horizonTicks,
        bool down,
        DevClassifyOptions const& opts,
        int* tickBudgetLeft = nullptr
    );

    // Delays to evaluate for a grid mode.
    static std::vector<int> buildGridDelays(
        DevConfig::EvalGrid grid,
        int limit,
        int plannedDelay,
        int robustK
    );

    // Run grid with primary H + optional tail T, then score with DevObjective.
    static GridReport evalGrid(
        PlayLayer* layer,
        DevLiveWitness const& witness,
        bool down,
        int limit,
        int plannedDelay,
        int robustK,
        int horizonTicks,
        int tailTicks,
        DevConfig::EvalGrid grid,
        DevObjective::Kind objective,
        DevClassifyOptions const& opts,
        float latencyWeight = 0.25f,
        int* tickBudgetLeft = nullptr
    );

    static bool selfCheck(std::string& report);

private:
    DevEvalEngine() = delete;

    static Report finishReport(
        DevLiveWitness const& witness,
        ShadowSimulator::EvalFromFreezeResult const& raw,
        int delay,
        int primaryHorizon,
        int tailTicks,
        DevClassifyOptions opts,
        DevSimPathId expectedPath
    );
};

#endif // DEV_EVAL_ENGINE_HPP
