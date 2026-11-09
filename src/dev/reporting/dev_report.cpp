#include "dev_report.hpp"
#include "../../config/stage_config.hpp"
#include "../../core/diagnostics/assist_sim_estimate.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../debug/lag_probe.hpp"
#include "../capture/dev_trace_store.hpp"
#include "../dev_config.hpp"
#include "../dev_session.hpp"
#include <Geode/Geode.hpp>
#include <cstring>
#include <fmt/format.h>

EbDevReport EbDevReportImplementation::s_lastReport{};
bool EbDevReportImplementation::s_hasLastReport = false;

char const* EbDevReportImplementation::fidShort(DevFidelity frame, bool approx) {
    switch (frame) {
    case DevFidelity::Reproduced:
        return "reproduced";
    case DevFidelity::PlayerOnlyApproximated:
        return "approx";
    case DevFidelity::NotReproducible:
        break;
    }
    (void)approx;
    return "norepro";
}

EbDevObjective::Kind EbDevReportImplementation::objectiveKindFromConfig() {
    switch (EbDevConfig::objective()) {
    case EbDevConfig::Objective::PlannerMirror:
        return EbDevObjective::Kind::PlannerMirror;
    case EbDevConfig::Objective::HorizonClear:
        return EbDevObjective::Kind::HorizonClear;
    case EbDevConfig::Objective::SurviveThenEndX:
        break;
    }
    return EbDevObjective::Kind::SurviveThenEndX;
}

EbDevDecisionEvent const* EbDevReportImplementation::findPlanFinished(EbDevAttempt const& attempt) {
    // Prefer last armed Delay with witness (counterfactual Net / bestSurv).
    // Falling back to last Pass plan made bestEval=0 look like a false "win".
    EbDevDecisionEvent const* bestDelay = nullptr;
    EbDevDecisionEvent const* bestAny = nullptr;
    for (auto const& e : attempt.events) {
        if (e.kind != DevEventKind::PlanFinished)
            continue;
        if (!e.hasWitness || !e.witness.valid)
            continue;
        bestAny = &e;
        if (e.plannedDelay > 0)
            bestDelay = &e;
    }
    return bestDelay ? bestDelay : bestAny;
}

int EbDevReportImplementation::limitFromPlanEvent(EbDevDecisionEvent const& e) {
    if (!e.candNotes.empty()) {
        int maxD = 0;
        for (auto const& n : e.candNotes) {
            if (n.delay > maxD)
                maxD = n.delay;
        }
        return maxD;
    }
    return std::max(0, e.plannedDelay);
}

bool EbDevReportImplementation::runEvalOnPlanEvent(PlayLayer* layer, EbDevDecisionEvent const& planEv,
                                                 EbDevReport& out, std::string& err) {
    if (!layer) {
        err = "no-playlayer";
        return false;
    }
    if (!EbShadowSimulator::get().isAttachedTo(layer)) {
        err = "sim-not-attached";
        return false;
    }
    if (!planEv.hasWitness || !planEv.witness.valid) {
        err = "no-witness";
        return false;
    }

    int limit = EbDevReportImplementation::limitFromPlanEvent(planEv);
    int const horizon = EbDevConfig::evalHorizon();
    int const tail = EbDevConfig::evalTail();
    int tickBudget = EbDevConfig::evalTickBudget();
    if (tickBudget < 1)
        tickBudget = 4000;

    EbDevClassifyOptions opts;
    opts.requireDeps = EbDevConfig::evalRequireDeps();
    opts.freezeWorld = EbDevConfig::freezeWorld();
    opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

    float latencyWeight = 0.25f;
    if (layer->m_player1) {
        latencyWeight = EbStageConfig::paramsFor(layer->m_player1).latencyWeight;
    }

    int robustK = EbStageConfig::robustK();
    if (robustK <= 0)
        robustK = 1;

    EbLagProbe::Scope const lag("eb.dev.eval");
    auto grid = EbDevEvalEngine::evalGrid(
        layer, planEv.witness, planEv.down, limit, planEv.plannedDelay, robustK, horizon, tail,
        EbDevConfig::evalGrid(), EbDevReportImplementation::objectiveKindFromConfig(), opts,
        latencyWeight, &tickBudget);

    std::string rec;
    if (EbDevSession::get().isRecording()) {
        rec = EbDevSession::get().statusLine();
    }
    out = EbDevReport::fromGrid(grid, std::move(rec), planEv.plannedDelay);
    if (out.valid && planEv.plannedDelay > 0 &&
        (!out.hasBestSurviving || out.bestSurvivingDelay < 0)) {
        out.liveVsEvalMismatch = true;
        out.livePlannedDelay = planEv.plannedDelay;
        out.overlayLine = EbDevReport::formatOverlayLine(out);
    }
    return out.valid;
}

EbDevReport EbDevReport::fromGrid(EbDevEvalEngine::GridReport const& grid, std::string recLine,
                              int plannedDelay) {
    EbDevReport r;
    r.recLine = std::move(recLine);
    r.fidelity = grid.fidelity.fidelity;
    r.claimsAllowed = grid.fidelity.claimsAllowed;
    r.approx = grid.fidelity.approx;
    r.objectiveId =
        grid.objective.objectiveId ? grid.objective.objectiveId : "survive_then_endx_v1";
    r.horizonTicks = grid.objective.horizonTicks;
    r.tailTicks = grid.objective.tailTicks;

    r.hasBestSurviving = grid.objective.hasBestSurviving;
    r.bestSurvivingDelay = grid.objective.bestSurvivingDelay;
    r.hasBestEvaluated = grid.objective.hasBestEvaluated;
    r.bestEvaluatedDelay = grid.objective.bestEvaluatedDelay;

    r.idealEligible = grid.objective.idealEligible;
    // Contract: idealDelay only when eligible — never invent the field.
    if (grid.objective.idealEligible && grid.objective.hasIdealDelay) {
        r.hasIdealDelay = true;
        r.idealDelay = grid.objective.idealDelay;
    } else {
        r.hasIdealDelay = false;
        r.idealDelay = -1;
        r.idealBlockedReason = grid.objective.idealBlockedReason;
    }

    // Commit 12: Prev/Caused from d0 vs planned Delay (simEstimate / G10).
    r.plannedDelay = plannedDelay;
    {
        std::vector<EbAssistSimEstimate::SampleView> views;
        views.reserve(grid.samples.size());
        for (auto const& s : grid.samples) {
            EbAssistSimEstimate::SampleView v;
            v.delay = s.delay;
            v.survivedPrimary = s.survivedPrimary;
            v.inconclusive = s.inconclusive();
            views.push_back(v);
        }
        auto const cls = EbAssistSimEstimate::classifyFromSamples(views, plannedDelay);
        r.simNetClass = EbAssistSimEstimate::netClassId(cls);
        if (plannedDelay > 0) {
            EbAssistSimEstimate::get().noteEval(cls, r.claimsAllowed);
        }
        auto& est = EbAssistSimEstimate::get();
        r.simPrev = est.prev();
        r.simCaused = est.caused();
        r.simNet = est.net();
        r.simHarmRate = est.harmRateSim();
    }

    r.overlayLine = formatOverlayLine(r);

    matjson::Value otherIndex = matjson::Value::object();
    if (!r.recLine.empty())
        otherIndex["rec"] = r.recLine;
    otherIndex["fidelity"] = EbDevStateDiff::fidelityId(r.fidelity);
    otherIndex["claimsAllowed"] = r.claimsAllowed;
    otherIndex["approx"] = r.approx;
    otherIndex["objectiveId"] = r.objectiveId;
    otherIndex["horizonTicks"] = r.horizonTicks;
    otherIndex["tailTicks"] = r.tailTicks;
    if (r.claimsAllowed && r.hasBestSurviving) {
        otherIndex["bestSurvivingDelay"] = r.bestSurvivingDelay;
    }
    if (r.claimsAllowed && r.hasBestEvaluated) {
        otherIndex["bestEvaluatedDelay"] = r.bestEvaluatedDelay;
    }
    otherIndex["idealEligible"] = r.idealEligible;
    if (r.idealEligible && r.hasIdealDelay) {
        otherIndex["idealDelay"] = r.idealDelay;
    } else if (!r.idealBlockedReason.empty()) {
        otherIndex["idealBlockedReason"] = r.idealBlockedReason;
    }
    if (!grid.fidelity.evalSuppressedReason.empty()) {
        otherIndex["evalSuppressedReason"] = grid.fidelity.evalSuppressedReason;
    }
    // G10: simEstimate fields — diagnostic only, never ship Pass/Fail.
    otherIndex["simEstimate"] = true;
    otherIndex["simNetClass"] = r.simNetClass ? r.simNetClass : "neutral";
    otherIndex["simPrev"] = r.simPrev;
    otherIndex["simCaused"] = r.simCaused;
    otherIndex["simNet"] = r.simNet;
    otherIndex["simHarmRate"] = r.simHarmRate;
    otherIndex["overlay"] = r.overlayLine;
    if (r.liveVsEvalMismatch) {
        otherIndex["liveVsEvalMismatch"] = true;
        otherIndex["livePlannedDelay"] = r.livePlannedDelay;
    }
    r.jsonLine = otherIndex.dump(0);
    r.valid = true;
    return r;
}

std::string EbDevReport::formatOverlayLine(EbDevReport const& r) {
    std::string line;
    if (!r.recLine.empty()) {
        line = r.recLine;
    }

    auto append = [&](std::string const& bit) {
        if (bit.empty())
            return;
        if (!line.empty())
            line += " | ";
        line += bit;
    };

    append(fmt::format("fid={}", EbDevReportImplementation::fidShort(r.fidelity, r.approx)));

    if (!r.claimsAllowed) {
        append("bestSurv=none");
        append(fmt::format("bestEval=none obj={}", r.objectiveId));
        if (r.liveVsEvalMismatch) {
            append(fmt::format("mismatch liveDelay={}", r.livePlannedDelay));
        }
        // Never emit ideal= when claims suppressed / not eligible.
        return line;
    }

    if (r.hasBestSurviving) {
        append(fmt::format("bestSurv={}", r.bestSurvivingDelay));
    } else {
        append("bestSurv=none");
    }
    if (r.liveVsEvalMismatch) {
        append(fmt::format("mismatch liveDelay={}", r.livePlannedDelay));
    }

    if (r.hasBestEvaluated) {
        // When nothing survives primary, bestEval is least-bad death — label it.
        if (!r.hasBestSurviving) {
            append(fmt::format("bestEval={}(died) obj={}", r.bestEvaluatedDelay, r.objectiveId));
        } else {
            append(fmt::format("bestEval={} obj={}", r.bestEvaluatedDelay, r.objectiveId));
        }
    } else {
        append(fmt::format("bestEval=none obj={}", r.objectiveId));
    }

    if (r.idealEligible && r.hasIdealDelay) {
        append(fmt::format("ideal={}", r.idealDelay));
    }

    // Commit 12: simEstimate Net secondary (never ship gate).
    if (r.plannedDelay > 0) {
        append(fmt::format("simEst {} net=+{}/-{} harm={:.0f}%",
                           r.simNetClass ? r.simNetClass : "neutral", r.simPrev, r.simCaused,
                           r.simHarmRate * 100.f));
    }
    return line;
}

void EbDevReport::setLast(EbDevReport report) {
    EbDevReportImplementation::s_lastReport = std::move(report);
    EbDevReportImplementation::s_hasLastReport = EbDevReportImplementation::s_lastReport.valid;
}

EbDevReport const& EbDevReport::last() {
    return EbDevReportImplementation::s_lastReport;
}

bool EbDevReport::hasLast() {
    return EbDevReportImplementation::s_hasLastReport && EbDevReportImplementation::s_lastReport.valid;
}

void EbDevReport::clearLast() {
    EbDevReportImplementation::s_lastReport = {};
    EbDevReportImplementation::s_hasLastReport = false;
}

bool EbDevReport::evalLastAttempt(std::string& err) {
    err.clear();
    auto* layer = PlayLayer::get();
    if (!layer) {
        err = "Enter a level to run eval.";
        return false;
    }

    auto& store = EbDevTraceStore::get();
    EbDevAttempt const* target = nullptr;

    if (store.isOpen()) {
        auto const& attempts = store.session().attempts;
        for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
            if (EbDevReportImplementation::findPlanFinished(*it)) {
                target = &*it;
                break;
            }
        }
    }
    if (!target) {
        err = "No attempt with a planFinished witness in the open session.";
        return false;
    }
    auto const* planEv = EbDevReportImplementation::findPlanFinished(*target);
    if (!planEv) {
        err = "No planFinished witness on attempt.";
        return false;
    }

    EbDevReport rep;
    if (!EbDevReportImplementation::runEvalOnPlanEvent(layer, *planEv, rep, err)) {
        if (err.empty())
            err = "eval-failed";
        return false;
    }
    setLast(std::move(rep));
    return true;
}

bool EbDevReport::evalSession(std::string& err, int* outCount) {
    err.clear();
    if (outCount)
        *outCount = 0;
    auto* layer = PlayLayer::get();
    if (!layer) {
        err = "Enter a level to run eval.";
        return false;
    }
    auto& store = EbDevTraceStore::get();
    if (!store.isOpen()) {
        err = "No open Dev session.";
        return false;
    }

    int ran = 0;
    EbDevReport lastOk;
    for (auto const& attempt : store.session().attempts) {
        if (!attempt.sealed && &attempt != &store.session().attempts.back()) {
            continue;
        }
        auto const* planEv = EbDevReportImplementation::findPlanFinished(attempt);
        if (!planEv)
            continue;
        EbDevReport rep;
        std::string localErr;
        if (!EbDevReportImplementation::runEvalOnPlanEvent(layer, *planEv, rep, localErr))
            continue;
        lastOk = std::move(rep);
        ++ran;
    }
    if (ran == 0) {
        err = "No evaluable planFinished events in session.";
        return false;
    }
    setLast(std::move(lastOk));
    if (outCount)
        *outCount = ran;
    return true;
}

bool EbDevReport::selfCheck(std::string& report) {
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

    // Synthetic: survive objective → no ideal= in overlay.
    {
        EbDevReport result;
        result.valid = true;
        result.claimsAllowed = true;
        result.fidelity = DevFidelity::Reproduced;
        result.objectiveId = "survive_then_endx_v1";
        result.hasBestSurviving = true;
        result.bestSurvivingDelay = 2;
        result.hasBestEvaluated = true;
        result.bestEvaluatedDelay = 2;
        result.idealEligible = false;
        result.hasIdealDelay = false;
        result.idealBlockedReason = "objective_not_horizon_clear";
        // Corrupt path must not emit ideal even if hasIdealDelay wrongly set.
        result.hasIdealDelay = true;
        result.idealDelay = 99;
        auto line = formatOverlayLine(result);
        expect(line.find("ideal=") == std::string::npos, "no-ideal-survive");
        expect(line.find("bestSurv=2") != std::string::npos, "bestsurv");
        expect(line.find("obj=survive_then_endx_v1") != std::string::npos, "obj");
        expect(line.find("fid=reproduced") != std::string::npos, "fid");
        expect(line.find("(died)") == std::string::npos, "no-died-tag-when-surv");
    }

    // No survivor → bestEval labeled (died).
    {
        EbDevReport result;
        result.claimsAllowed = true;
        result.fidelity = DevFidelity::Reproduced;
        result.objectiveId = "survive_then_endx_v1";
        result.hasBestSurviving = false;
        result.hasBestEvaluated = true;
        result.bestEvaluatedDelay = 0;
        auto line = formatOverlayLine(result);
        expect(line.find("bestSurv=none") != std::string::npos, "nosurv");
        expect(line.find("bestEval=0(died)") != std::string::npos, "died-tag");
    }

    // Eligible horizon_clear → ideal= present.
    {
        EbDevReport result;
        result.claimsAllowed = true;
        result.fidelity = DevFidelity::Reproduced;
        result.objectiveId = "horizon_clear_v1";
        result.hasBestSurviving = true;
        result.bestSurvivingDelay = 1;
        result.hasBestEvaluated = true;
        result.bestEvaluatedDelay = 1;
        result.idealEligible = true;
        result.hasIdealDelay = true;
        result.idealDelay = 1;
        auto line = formatOverlayLine(result);
        expect(line.find("ideal=1") != std::string::npos, "ideal-ok");
    }

    // fromGrid must strip ideal when not eligible even if Result has numbers.
    {
        EbDevEvalEngine::GridReport gr;
        gr.fidelity.fidelity = DevFidelity::Reproduced;
        gr.fidelity.claimsAllowed = true;
        gr.objective.objectiveId = "survive_then_endx_v1";
        gr.objective.idealEligible = false;
        gr.objective.hasIdealDelay = true; // buggy input — must ignore
        gr.objective.idealDelay = 7;
        gr.objective.idealBlockedReason = "objective_not_horizon_clear";
        gr.objective.hasBestSurviving = true;
        gr.objective.bestSurvivingDelay = 0;
        gr.objective.hasBestEvaluated = true;
        gr.objective.bestEvaluatedDelay = 0;
        auto result = fromGrid(gr, "REC 1/10");
        expect(result.valid, "from-valid");
        expect(!result.hasIdealDelay, "from-no-ideal-flag");
        expect(result.overlayLine.find("ideal=") == std::string::npos, "from-no-ideal");
        expect(result.jsonLine.find("\"idealDelay\"") == std::string::npos, "from-json-no-ideal");
        expect(result.overlayLine.find("REC 1/10") != std::string::npos, "from-rec");
        expect(result.jsonLine.find("simEstimate") != std::string::npos, "sim-tag");
    }

    // Commit 12: Prev class from d0 die / delay survive → simEst overlay.
    {
        EbAssistSimEstimate::get().resetSession();
        EbDevEvalEngine::GridReport gr;
        gr.fidelity.fidelity = DevFidelity::Reproduced;
        gr.fidelity.claimsAllowed = true;
        gr.objective.objectiveId = "survive_then_endx_v1";
        gr.objective.hasBestSurviving = true;
        gr.objective.bestSurvivingDelay = 4;
        EbDevObjective::Sample d0;
        d0.delay = 0;
        d0.survivedPrimary = false;
        EbDevObjective::Sample arm;
        arm.delay = 4;
        arm.survivedPrimary = true;
        gr.samples = {d0, arm};
        auto result = fromGrid(gr, "", 4);
        expect(std::string(result.simNetClass) == "prev", "sim-prev");
        expect(result.overlayLine.find("simEst") != std::string::npos, "sim-ov");
        expect(EbAssistSimEstimate::get().prev() == 1, "sim-acc");
        EbAssistSimEstimate::get().resetSession();
    }

    // Claims off → bestSurv=none, no ideal.
    {
        EbDevReport result;
        result.claimsAllowed = false;
        result.fidelity = DevFidelity::NotReproducible;
        result.objectiveId = "horizon_clear_v1";
        result.idealEligible = true;
        result.hasIdealDelay = true;
        result.idealDelay = 3;
        auto line = formatOverlayLine(result);
        expect(line.find("bestSurv=none") != std::string::npos, "suppress-surv");
        expect(line.find("ideal=") == std::string::npos, "suppress-ideal");
        expect(line.find("fid=norepro") != std::string::npos, "suppress-fid");
    }

    if (fails == 0) {
        report = "DevReport selfCheck OK";
        return true;
    }
    report = std::string("DevReport selfCheck fails: ") + report;
    return false;
}
