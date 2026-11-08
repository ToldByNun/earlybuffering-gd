#include "dev_eval_engine.hpp"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <unordered_set>

DevEvalEngine::Report DevEvalEngine::finishReport(DevLiveWitness const& witness,
                                                  ShadowSimulator::EvalFromFreezeResult const& raw,
                                                  int delay, int primaryHorizon, int tailTicks,
                                                  DevClassifyOptions options,
                                                  DevSimPathId expectedPath) {
    Report rep;
    rep.timeBasis = DevClockContract::kTimeBasisPhysicsTick;
    rep.clockSchema = DevClockContract::kSchema;
    rep.liveSimPathId = DevClockContract::simPathId(witness.clock.liveSimPathId);
    rep.evalSimPathId = DevClockContract::simPathId(raw.evalSimPathId);
    rep.stepScheduleHash = raw.stepScheduleHash;
    rep.clockMismatch = raw.clockMismatch;
    rep.restore = raw.restore;
    rep.ran = raw.ran;

    DevObjective::classifyPrimaryAndTail(rep.sample, delay, primaryHorizon, tailTicks, raw.died,
                                         raw.deathTick, raw.endX, raw.unknown, raw.budgetHit,
                                         raw.aborted, raw.ticksRan);

    options.expectedEvalPath = expectedPath;

    if (raw.clockMismatch) {
        rep.fidelity.fidelity = DevFidelity::NotReproducible;
        rep.fidelity.claimsAllowed = false;
        rep.fidelity.approx = false;
        rep.fidelity.evalSuppressedReason = "clockMismatch";
        return rep;
    }

    DevClassifyResult gate = DevStateDiff::classifyWitnessGate(witness, options);
    if (!gate.evalSuppressedReason.empty()) {
        rep.fidelity = gate;
        return rep;
    }

    if (raw.ran && raw.restore.valid) {
        rep.fidelity = DevStateDiff::classify(witness, raw.restore, options);
    } else {
        rep.fidelity.fidelity = DevFidelity::NotReproducible;
        rep.fidelity.claimsAllowed = false;
        rep.fidelity.evalSuppressedReason = raw.aborted ? "aborted" : "evalDidNotRun";
    }
    return rep;
}

DevEvalEngine::Report DevEvalEngine::evalDelay(PlayLayer* layer, DevLiveWitness const& witness,
                                               int delay, int horizonTicks, bool down,
                                               DevClassifyOptions const& opts,
                                               int* tickBudgetLeft) {
    // horizonTicks here is the schedule horizon after action (primary only).
    auto rawData = ShadowSimulator::get().evalDelayFromFreeze(layer, witness, delay, horizonTicks,
                                                              down, tickBudgetLeft);
    return finishReport(witness, rawData, delay, horizonTicks,
                        /*tailTicks*/ 0, opts, DevSimPathId::EvalDelayFromFreeze);
}

DevEvalEngine::Report DevEvalEngine::replayActualFire(PlayLayer* layer,
                                                      DevLiveWitness const& witness,
                                                      int actualFireTickAbs, int horizonTicks,
                                                      bool down, DevClassifyOptions const& opts,
                                                      int* tickBudgetLeft) {
    auto rawData = ShadowSimulator::get().replayActualFire(layer, witness, actualFireTickAbs,
                                                           horizonTicks, down, tickBudgetLeft);
    int delay = actualFireTickAbs - witness.clock.effectTick;
    if (delay < 0)
        delay = 0;
    return finishReport(witness, rawData, delay, horizonTicks,
                        /*tailTicks*/ 0, opts, DevSimPathId::ReplayActualFire);
}

std::vector<int> DevEvalEngine::buildGridDelays(DevConfig::EvalGrid grid, int limit,
                                                int plannedDelay, int robustK) {
    if (limit < 0)
        limit = 0;
    std::unordered_set<int> set;
    auto add = [&](int delay) {
        if (delay < 0 || delay > limit)
            return;
        set.insert(delay);
    };

    switch (grid) {
    case DevConfig::EvalGrid::Full:
        for (int delay = 0; delay <= limit; ++delay)
            add(delay);
        break;
    case DevConfig::EvalGrid::Sparse: {
        add(0);
        add(limit / 2);
        add(limit);
        add(plannedDelay);
        for (int delay = plannedDelay - 2; delay <= plannedDelay + 2; ++delay)
            add(delay);
        break;
    }
    case DevConfig::EvalGrid::Band: {
        add(0);
        add(plannedDelay);
        int const offset = std::max(0, robustK);
        for (int delay = plannedDelay - offset; delay <= plannedDelay + offset; ++delay)
            add(delay);
        // First-wave survivors band is refined later; seed ±k around plan + 0.
        break;
    }
    }

    std::vector<int> output(set.begin(), set.end());
    std::sort(output.begin(), output.end());
    return output;
}

DevEvalEngine::GridReport
DevEvalEngine::evalGrid(PlayLayer* layer, DevLiveWitness const& witness, bool down, int limit,
                        int plannedDelay, int robustK, int horizonTicks, int tailTicks,
                        DevConfig::EvalGrid grid, DevObjective::Kind objective,
                        DevClassifyOptions const& opts, float latencyWeight, int* tickBudgetLeft) {
    GridReport gr;
    gr.timeBasis = DevClockContract::kTimeBasisPhysicsTick;
    gr.clockSchema = DevClockContract::kSchema;
    gr.liveSimPathId = DevClockContract::simPathId(witness.clock.liveSimPathId);
    gr.evalSimPathId = DevClockContract::simPathId(DevSimPathId::EvalDelayFromFreeze);
    gr.grid = grid;
    gr.horizonTicks = horizonTicks;
    gr.tailTicks = tailTicks;
    gr.limit = limit;
    gr.objectivePartial = (grid != DevConfig::EvalGrid::Full);

    if (!witness.valid || !witness.clock.valid || !DevClockContract::get().clockOk(witness.clock)) {
        gr.clockMismatch = true;
        gr.fidelity.fidelity = DevFidelity::NotReproducible;
        gr.fidelity.claimsAllowed = false;
        gr.fidelity.evalSuppressedReason = "clockMismatch";
        DevObjective::Meta meta;
        meta.claimsAllowed = false;
        meta.fidelity = DevFidelity::NotReproducible;
        meta.clockOk = false;
        meta.gridFull = false;
        meta.objectivePartial = true;
        meta.horizonTicks = horizonTicks;
        meta.tailTicks = tailTicks;
        gr.objective = DevObjective::evaluate(objective, gr.samples, meta);
        return gr;
    }

    auto delays = buildGridDelays(grid, limit, plannedDelay, robustK);
    int const schedHorizon = horizonTicks + std::max(0, tailTicks);
    bool anyRan = false;
    bool tailsComplete = true;

    ShadowSimulator::get().clearEvalAbort();
    for (int delay : delays) {
        if (ShadowSimulator::get().evalAbortRequested())
            break;
        auto rawData = ShadowSimulator::get().evalDelayFromFreeze(
            layer, witness, delay, schedHorizon, down, tickBudgetLeft);
        if (rawData.clockMismatch) {
            gr.clockMismatch = true;
            break;
        }
        if (!gr.restore.valid && rawData.restore.valid) {
            gr.restore = rawData.restore;
        }
        if (rawData.stepScheduleHash != 0) {
            gr.stepScheduleHash = rawData.stepScheduleHash;
        }

        DevObjective::Sample sample;
        DevObjective::classifyPrimaryAndTail(sample, delay, horizonTicks, tailTicks, rawData.died,
                                             rawData.deathTick, rawData.endX, rawData.unknown,
                                             rawData.budgetHit, rawData.aborted, rawData.ticksRan);
        if (sample.survivedPrimary && tailTicks > 0 && !sample.tailComplete) {
            tailsComplete = false;
        }
        if (rawData.ran)
            anyRan = true;
        gr.samples.push_back(sample);

        if (tickBudgetLeft && *tickBudgetLeft <= 0) {
            gr.objectivePartial = true;
            break;
        }
    }

    // Fidelity from restore (shared across delays) or witness gate.
    if (gr.clockMismatch) {
        gr.fidelity.fidelity = DevFidelity::NotReproducible;
        gr.fidelity.claimsAllowed = false;
        gr.fidelity.evalSuppressedReason = "clockMismatch";
    } else {
        DevClassifyResult gate = DevStateDiff::classifyWitnessGate(witness, opts);
        if (!gate.evalSuppressedReason.empty()) {
            gr.fidelity = gate;
        } else if (anyRan && gr.restore.valid) {
            DevClassifyOptions outputStream = opts;
            outputStream.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;
            gr.fidelity = DevStateDiff::classify(witness, gr.restore, outputStream);
        } else {
            gr.fidelity.fidelity = DevFidelity::NotReproducible;
            gr.fidelity.claimsAllowed = false;
            gr.fidelity.evalSuppressedReason = "evalDidNotRun";
        }
    }

    bool const gridFull = (grid == DevConfig::EvalGrid::Full) &&
                          static_cast<int>(delays.size()) == (limit + 1) && !gr.objectivePartial;
    for (auto const& s : gr.samples) {
        if (s.budgetHit) {
            gr.objectivePartial = true;
        }
    }

    DevObjective::Meta meta;
    meta.fidelity = gr.fidelity.fidelity;
    meta.claimsAllowed = gr.fidelity.claimsAllowed;
    meta.clockOk = !gr.clockMismatch && DevClockContract::get().clockOk(witness.clock);
    meta.gridFull = gridFull && !gr.objectivePartial;
    meta.tailsComplete = tailsComplete;
    meta.horizonTicks = horizonTicks;
    meta.tailTicks = tailTicks;
    meta.latencyWeight = latencyWeight;
    meta.objectivePartial = gr.objectivePartial;

    gr.objective = DevObjective::evaluate(objective, gr.samples, meta);
    return gr;
}

bool DevEvalEngine::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    DevClassifyOptions opts;
    opts.requireDeps = true;
    opts.freezeWorld = false;
    opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

    // Clock mismatch → notReproducible / clockMismatch.
    {
        DevLiveWitness w;
        w.valid = true;
        w.player.present = true;
        w.player.x = 1.f;
        w.playerBlobSchema = DevLiveWitness::computePlayerBlobSchema();
        w.clock = DevClockContract::get().makeStamp(10, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        w.clock.effectTick = 999;
        w.clock.valid = true;
        w.worldDeps = {
            static_cast<int>(DevWitnessCategory::Player),
            static_cast<int>(DevWitnessCategory::Clock),
        };

        Report report = evalDelay(nullptr, w, 2, 8, true, opts, nullptr);
        expect(report.clockMismatch, "clock-mismatch-flag");
        expect(report.fidelity.fidelity == DevFidelity::NotReproducible, "clock-mismatch-fid");
        expect(report.fidelity.evalSuppressedReason == "clockMismatch", "clock-mismatch-reason");
        expect(report.timeBasis == DevClockContract::kTimeBasisPhysicsTick, "time-basis");
        expect(report.clockSchema == DevClockContract::kSchema, "clock-schema");
    }

    // Grid delay builders.
    {
        auto full = buildGridDelays(DevConfig::EvalGrid::Full, 4, 2, 1);
        expect(full.size() == 5, "grid-full-n");
        auto band = buildGridDelays(DevConfig::EvalGrid::Band, 10, 5, 2);
        expect(std::find(band.begin(), band.end(), 0) != band.end() &&
                   std::find(band.begin(), band.end(), 5) != band.end() &&
                   std::find(band.begin(), band.end(), 3) != band.end() &&
                   std::find(band.begin(), band.end(), 7) != band.end(),
               "grid-band");
        auto sparse = buildGridDelays(DevConfig::EvalGrid::Sparse, 10, 4, 1);
        expect(std::find(sparse.begin(), sparse.end(), 0) != sparse.end() &&
                   std::find(sparse.begin(), sparse.end(), 10) != sparse.end(),
               "grid-sparse");
    }

    // Objective integration via evalGrid with empty layer (no samples run → suppressed).
    {
        DevLiveWitness w;
        w.valid = true;
        w.player.present = true;
        w.playerBlobSchema = DevLiveWitness::computePlayerBlobSchema();
        w.clock = DevClockContract::get().makeStamp(3, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        auto gr = evalGrid(nullptr, w, true, 3, 1, 1, 8, 8, DevConfig::EvalGrid::Band,
                           DevObjective::Kind::SurviveThenEndX, opts, 0.25f, nullptr);
        expect(gr.objectivePartial, "grid-partial-band");
        expect(gr.objective.objectiveId == std::string("survive_then_endx_v1"), "grid-obj-id");
        // No ghosts → samples empty / not claimsAllowed.
        expect(!gr.objective.hasIdealDelay, "grid-no-ideal");
    }

    // abort clears on eval entry.
    {
        ShadowSimulator::get().abortEval();
        expect(ShadowSimulator::get().evalAbortRequested(), "abort-set");
        DevLiveWitness w;
        w.valid = true;
        w.player.present = true;
        w.playerBlobSchema = DevLiveWitness::computePlayerBlobSchema();
        w.clock = DevClockContract::get().makeStamp(1, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        (void)ShadowSimulator::get().evalDelayFromFreeze(nullptr, w, 0, 1, true, nullptr);
        expect(!ShadowSimulator::get().evalAbortRequested(), "abort-cleared");
    }

    expect(std::strcmp(DevClockContract::simPathId(DevSimPathId::EvalDelayFromFreeze),
                       "ShadowSimulator.evalDelayFromFreeze") == 0,
           "path-const");

    if (fails == 0) {
        report = "DevEvalEngine selfCheck OK";
        return true;
    }
    report = std::string("DevEvalEngine selfCheck fails: ") + report;
    return false;
}
