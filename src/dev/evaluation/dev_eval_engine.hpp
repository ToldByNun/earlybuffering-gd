#ifndef DEV_EVAL_ENGINE_HPP
#define DEV_EVAL_ENGINE_HPP

#include "../../core/sim/shadow_simulator.hpp"
#include "../capture/dev_clock_contract.hpp"
#include "../capture/dev_live_witness.hpp"
#include "../dev_config.hpp"
#include "dev_eval_restore.hpp"
#include "dev_objective.hpp"
#include "dev_state_diff.hpp"
#include <cstdint>
#include <string>
#include <vector>

class PlayLayer;

// Clock-bound eval + grid/tail/objectives (Commits 9–10).
class EbDevEvalEngine {
public:
    class Report {
    public:
        std::string timeBasis;
        int clockSchema = 0;
        std::string liveSimPathId;
        std::string evalSimPathId;
        uint64_t stepScheduleHash = 0;
        bool clockMismatch = false;
        EbDevClassifyResult fidelity{};
        EbDevEvalRestore restore{};
        EbDevObjective::Sample sample{};
        bool ran = false;

    private:
    };

    class GridReport {
    public:
        std::string timeBasis;
        int clockSchema = 0;
        std::string liveSimPathId;
        std::string evalSimPathId;
        uint64_t stepScheduleHash = 0;
        bool clockMismatch = false;
        EbDevClassifyResult fidelity{};
        EbDevEvalRestore restore{};
        std::vector<EbDevObjective::Sample> samples;
        EbDevObjective::Result objective{};
        bool objectivePartial = false;
        EbDevConfig::EvalGrid grid = EbDevConfig::EvalGrid::Band;
        int horizonTicks = 48;
        int tailTicks = 48;
        int limit = 0;

    private:
    };

    // Counterfactual delay on the witness stamp (evalDelayFromFreeze).
    // Pass horizonTicks = H (primary only) or H+T (combined; use classifyPrimaryAndTail).
    static Report evalDelay(PlayLayer* layer, EbDevLiveWitness const& witness, int delay,
                            int horizonTicks, bool down, EbDevClassifyOptions const& opts,
                            int* tickBudgetLeft = nullptr);

    // Replay absolute actualFireTick on the same stamp.
    static Report replayActualFire(PlayLayer* layer, EbDevLiveWitness const& witness,
                                   int actualFireTickAbs, int horizonTicks, bool down,
                                   EbDevClassifyOptions const& opts, int* tickBudgetLeft = nullptr);

    // Delays to evaluate for a grid mode.
    static std::vector<int> buildGridDelays(EbDevConfig::EvalGrid grid, int limit, int plannedDelay,
                                            int robustK);

    // Run grid with primary H + optional tail T, then score with DevObjective.
    static GridReport evalGrid(PlayLayer* layer, EbDevLiveWitness const& witness, bool down,
                               int limit, int plannedDelay, int robustK, int horizonTicks,
                               int tailTicks, EbDevConfig::EvalGrid grid,
                               EbDevObjective::Kind objective, EbDevClassifyOptions const& opts,
                               float latencyWeight = 0.25f, int* tickBudgetLeft = nullptr,
                               float firePhase = 0.f);

    static bool selfCheck(std::string& report);

private:
    EbDevEvalEngine() = delete;

    static Report finishReport(EbDevLiveWitness const& witness,
                               EbShadowSimulator::EvalFromFreezeResult const& raw, int delay,
                               int primaryHorizon, int tailTicks, EbDevClassifyOptions opts,
                               DevSimPathId expectedPath);
};

#endif
