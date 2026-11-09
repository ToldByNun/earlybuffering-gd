#include "dev_eval_engine.hpp"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <unordered_set>

EbDevEvalEngine::Report EbDevEvalEngine::finishReport(EbDevLiveWitness const& witness,
                                                  EbShadowSimulator::EvalFromFreezeResult const& raw,
                                                  int delay, int primaryHorizon, int tailTicks,
                                                  EbDevClassifyOptions options,
                                                  DevSimPathId expectedPath) {
    Report rep;
    rep.timeBasis = EbDevClockContract::kTimeBasisPhysicsTick;
    rep.clockSchema = EbDevClockContract::kSchema;
    rep.liveSimPathId = EbDevClockContract::simPathId(witness.clock.liveSimPathId);
    rep.evalSimPathId = EbDevClockContract::simPathId(raw.evalSimPathId);
    rep.stepScheduleHash = raw.stepScheduleHash;
    rep.clockMismatch = raw.clockMismatch;
    rep.restore = raw.restore;
    rep.ran = raw.ran;

    EbDevObjective::classifyPrimaryAndTail(rep.sample, delay, primaryHorizon, tailTicks, raw.died,
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

    EbDevClassifyResult gate = EbDevStateDiff::classifyWitnessGate(witness, options);
    if (!gate.evalSuppressedReason.empty()) {
        rep.fidelity = gate;
        return rep;
    }

    if (raw.ran && raw.restore.valid) {
        rep.fidelity = EbDevStateDiff::classify(witness, raw.restore, options);
    } else {
        rep.fidelity.fidelity = DevFidelity::NotReproducible;
        rep.fidelity.claimsAllowed = false;
        rep.fidelity.evalSuppressedReason = raw.aborted ? "aborted" : "evalDidNotRun";
    }
    return rep;
}

EbDevEvalEngine::Report EbDevEvalEngine::evalDelay(PlayLayer* layer, EbDevLiveWitness const& witness,
                                               int delay, int horizonTicks, bool down,
                                               EbDevClassifyOptions const& opts,
                                               int* tickBudgetLeft) {
    // horizonTicks here is the schedule horizon after action (primary only).
    auto rawData = EbShadowSimulator::get().evalDelayFromFreeze(layer, witness, delay, horizonTicks,
                                                              down, tickBudgetLeft);
    return finishReport(witness, rawData, delay, horizonTicks,
                        /*tailTicks*/ 0, opts, DevSimPathId::EvalDelayFromFreeze);
}

EbDevEvalEngine::Report EbDevEvalEngine::replayActualFire(PlayLayer* layer,
                                                      EbDevLiveWitness const& witness,
                                                      int actualFireTickAbs, int horizonTicks,
                                                      bool down, EbDevClassifyOptions const& opts,
                                                      int* tickBudgetLeft) {
    auto rawData = EbShadowSimulator::get().replayActualFire(layer, witness, actualFireTickAbs,
                                                           horizonTicks, down, tickBudgetLeft);
    int delay = actualFireTickAbs - witness.clock.effectTick;
    if (delay < 0)
        delay = 0;
    return finishReport(witness, rawData, delay, horizonTicks,
                        /*tailTicks*/ 0, opts, DevSimPathId::ReplayActualFire);
}

std::vector<int> EbDevEvalEngine::buildGridDelays(EbDevConfig::EvalGrid grid, int limit,
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
    case EbDevConfig::EvalGrid::Full:
        for (int delay = 0; delay <= limit; ++delay)
            add(delay);
        break;
    case EbDevConfig::EvalGrid::Sparse: {
        add(0);
        add(limit / 2);
        add(limit);
        add(plannedDelay);
        for (int delay = plannedDelay - 2; delay <= plannedDelay + 2; ++delay)
            add(delay);
        break;
    }
    case EbDevConfig::EvalGrid::Band: {
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

EbDevEvalEngine::GridReport
EbDevEvalEngine::evalGrid(PlayLayer* layer, EbDevLiveWitness const& witness, bool down, int limit,
                        int plannedDelay, int robustK, int horizonTicks, int tailTicks,
                        EbDevConfig::EvalGrid grid, EbDevObjective::Kind objective,
                        EbDevClassifyOptions const& opts, float latencyWeight, int* tickBudgetLeft) {
    GridReport gr;
    gr.timeBasis = EbDevClockContract::kTimeBasisPhysicsTick;
    gr.clockSchema = EbDevClockContract::kSchema;
    gr.liveSimPathId = EbDevClockContract::simPathId(witness.clock.liveSimPathId);
    gr.evalSimPathId = EbDevClockContract::simPathId(DevSimPathId::EvalDelayFromFreeze);
    gr.grid = grid;
    gr.horizonTicks = horizonTicks;
    gr.tailTicks = tailTicks;
    gr.limit = limit;
    gr.objectivePartial = (grid != EbDevConfig::EvalGrid::Full);

    if (!witness.valid || !witness.clock.valid || !EbDevClockContract::get().clockOk(witness.clock)) {
        gr.clockMismatch = true;
        gr.fidelity.fidelity = DevFidelity::NotReproducible;
        gr.fidelity.claimsAllowed = false;
        gr.fidelity.evalSuppressedReason = "clockMismatch";
        EbDevObjective::Meta meta;
        meta.claimsAllowed = false;
        meta.fidelity = DevFidelity::NotReproducible;
        meta.clockOk = false;
        meta.gridFull = false;
        meta.objectivePartial = true;
        meta.horizonTicks = horizonTicks;
        meta.tailTicks = tailTicks;
        gr.objective = EbDevObjective::evaluate(objective, gr.samples, meta);
        return gr;
    }

    auto delays = buildGridDelays(grid, limit, plannedDelay, robustK);
    int const schedHorizon = horizonTicks + std::max(0, tailTicks);
    bool anyRan = false;
    bool tailsComplete = true;

    EbShadowSimulator::get().clearEvalAbort();
    for (int delay : delays) {
        if (EbShadowSimulator::get().evalAbortRequested())
            break;
        auto rawData = EbShadowSimulator::get().evalDelayFromFreeze(
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

        EbDevObjective::Sample sample;
        EbDevObjective::classifyPrimaryAndTail(sample, delay, horizonTicks, tailTicks, rawData.died,
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
        EbDevClassifyResult gate = EbDevStateDiff::classifyWitnessGate(witness, opts);
        if (!gate.evalSuppressedReason.empty()) {
            gr.fidelity = gate;
        } else if (anyRan && gr.restore.valid) {
            EbDevClassifyOptions outputStream = opts;
            outputStream.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;
            gr.fidelity = EbDevStateDiff::classify(witness, gr.restore, outputStream);
        } else {
            gr.fidelity.fidelity = DevFidelity::NotReproducible;
            gr.fidelity.claimsAllowed = false;
            gr.fidelity.evalSuppressedReason = "evalDidNotRun";
        }
    }

    bool const gridFull = (grid == EbDevConfig::EvalGrid::Full) &&
                          static_cast<int>(delays.size()) == (limit + 1) && !gr.objectivePartial;
    for (auto const& s : gr.samples) {
        if (s.budgetHit) {
            gr.objectivePartial = true;
        }
    }

    EbDevObjective::Meta meta;
    meta.fidelity = gr.fidelity.fidelity;
    meta.claimsAllowed = gr.fidelity.claimsAllowed;
    meta.clockOk = !gr.clockMismatch && EbDevClockContract::get().clockOk(witness.clock);
    meta.gridFull = gridFull && !gr.objectivePartial;
    meta.tailsComplete = tailsComplete;
    meta.horizonTicks = horizonTicks;
    meta.tailTicks = tailTicks;
    meta.latencyWeight = latencyWeight;
    meta.objectivePartial = gr.objectivePartial;

    gr.objective = EbDevObjective::evaluate(objective, gr.samples, meta);
    return gr;
}

bool EbDevEvalEngine::selfCheck(std::string& report) {
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

    EbDevClassifyOptions opts;
    opts.requireDeps = true;
    opts.freezeWorld = false;
    opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

    // Clock mismatch → notReproducible / clockMismatch.
    {
        EbDevLiveWitness w;
        w.valid = true;
        w.player.present = true;
        w.player.x = 1.f;
        w.playerBlobSchema = EbDevLiveWitness::computePlayerBlobSchema();
        w.clock = EbDevClockContract::get().makeStamp(10, false, false, 1.f / 240.f, 1.f / 240.f,
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
        expect(report.timeBasis == EbDevClockContract::kTimeBasisPhysicsTick, "time-basis");
        expect(report.clockSchema == EbDevClockContract::kSchema, "clock-schema");
    }

    // Grid delay builders.
    {
        auto full = buildGridDelays(EbDevConfig::EvalGrid::Full, 4, 2, 1);
        expect(full.size() == 5, "grid-full-n");
        auto band = buildGridDelays(EbDevConfig::EvalGrid::Band, 10, 5, 2);
        expect(std::find(band.begin(), band.end(), 0) != band.end() &&
                   std::find(band.begin(), band.end(), 5) != band.end() &&
                   std::find(band.begin(), band.end(), 3) != band.end() &&
                   std::find(band.begin(), band.end(), 7) != band.end(),
               "grid-band");
        auto sparse = buildGridDelays(EbDevConfig::EvalGrid::Sparse, 10, 4, 1);
        expect(std::find(sparse.begin(), sparse.end(), 0) != sparse.end() &&
                   std::find(sparse.begin(), sparse.end(), 10) != sparse.end(),
               "grid-sparse");
    }

    // Objective integration via evalGrid with empty layer (no samples run → suppressed).
    {
        EbDevLiveWitness w;
        w.valid = true;
        w.player.present = true;
        w.playerBlobSchema = EbDevLiveWitness::computePlayerBlobSchema();
        w.clock = EbDevClockContract::get().makeStamp(3, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        auto gr = evalGrid(nullptr, w, true, 3, 1, 1, 8, 8, EbDevConfig::EvalGrid::Band,
                           EbDevObjective::Kind::SurviveThenEndX, opts, 0.25f, nullptr);
        expect(gr.objectivePartial, "grid-partial-band");
        expect(gr.objective.objectiveId == std::string("survive_then_endx_v1"), "grid-obj-id");
        // No ghosts → samples empty / not claimsAllowed.
        expect(!gr.objective.hasIdealDelay, "grid-no-ideal");
    }

    // abort clears on eval entry.
    {
        EbShadowSimulator::get().abortEval();
        expect(EbShadowSimulator::get().evalAbortRequested(), "abort-set");
        EbDevLiveWitness w;
        w.valid = true;
        w.player.present = true;
        w.playerBlobSchema = EbDevLiveWitness::computePlayerBlobSchema();
        w.clock = EbDevClockContract::get().makeStamp(1, false, false, 1.f / 240.f, 1.f / 240.f,
                                                    1.f / 240.f, 1.f, DevSimPathId::Plan);
        (void)EbShadowSimulator::get().evalDelayFromFreeze(nullptr, w, 0, 1, true, nullptr);
        expect(!EbShadowSimulator::get().evalAbortRequested(), "abort-cleared");
    }

    expect(std::strcmp(EbDevClockContract::simPathId(DevSimPathId::EvalDelayFromFreeze),
                       "ShadowSimulator.evalDelayFromFreeze") == 0,
           "path-const");

    if (fails == 0) {
        report = "DevEvalEngine selfCheck OK";
        return true;
    }
    report = std::string("DevEvalEngine selfCheck fails: ") + report;
    return false;
}
