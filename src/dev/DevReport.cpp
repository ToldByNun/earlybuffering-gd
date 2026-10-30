#include "DevReport.hpp"
#include "DevConfig.hpp"
#include "DevSession.hpp"
#include "DevTraceStore.hpp"
#include "../debug/LagProbe.hpp"
#include "../core/ShadowSimulator.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <cstring>
#include <fmt/format.h>

using namespace geode::prelude;

namespace {
DevReport g_lastReport{};
bool g_hasLastReport = false;

char const* fidShort(DevFidelity f, bool approx) {
    switch (f) {
        case DevFidelity::Reproduced: return "reproduced";
        case DevFidelity::PlayerOnlyApproximated: return "approx";
        case DevFidelity::NotReproducible: break;
    }
    (void)approx;
    return "norepro";
}

DevObjective::Kind objectiveKindFromConfig() {
    switch (DevConfig::objective()) {
        case DevConfig::Objective::PlannerMirror:
            return DevObjective::Kind::PlannerMirror;
        case DevConfig::Objective::HorizonClear:
            return DevObjective::Kind::HorizonClear;
        case DevConfig::Objective::SurviveThenEndX:
            break;
    }
    return DevObjective::Kind::SurviveThenEndX;
}

DevDecisionEvent const* findPlanFinished(DevAttempt const& attempt) {
    DevDecisionEvent const* best = nullptr;
    for (auto const& e : attempt.events) {
        if (e.kind != DevEventKind::PlanFinished) continue;
        if (!e.hasWitness || !e.witness.valid) continue;
        best = &e;
    }
    return best;
}

int limitFromPlanEvent(DevDecisionEvent const& e) {
    if (!e.candNotes.empty()) {
        int maxD = 0;
        for (auto const& n : e.candNotes) {
            if (n.delay > maxD) maxD = n.delay;
        }
        return maxD;
    }
    return std::max(0, e.plannedDelay);
}

bool runEvalOnPlanEvent(
    PlayLayer* layer,
    DevDecisionEvent const& planEv,
    DevReport& out,
    std::string& err
) {
    if (!layer) {
        err = "no-playlayer";
        return false;
    }
    if (!ShadowSimulator::get().isAttachedTo(layer)) {
        err = "sim-not-attached";
        return false;
    }
    if (!planEv.hasWitness || !planEv.witness.valid) {
        err = "no-witness";
        return false;
    }

    int limit = limitFromPlanEvent(planEv);
    int const horizon = DevConfig::evalHorizon();
    int const tail = DevConfig::evalTail();
    int tickBudget = DevConfig::evalTickBudget();
    if (tickBudget < 1) tickBudget = 4000;

    DevClassifyOptions opts;
    opts.requireDeps = DevConfig::evalRequireDeps();
    opts.freezeWorld = DevConfig::freezeWorld();
    opts.expectedEvalPath = DevSimPathId::EvalDelayFromFreeze;

    float latencyWeight = 0.25f;
    if (layer->m_player1) {
        latencyWeight = StageConfig::paramsFor(layer->m_player1).latencyWeight;
    }

    int robustK = StageConfig::robustK();
    if (robustK <= 0) robustK = 1;

    LagProbe::Scope const lag("eb.dev.eval");
    auto grid = DevEvalEngine::evalGrid(
        layer,
        planEv.witness,
        planEv.down,
        limit,
        planEv.plannedDelay,
        robustK,
        horizon,
        tail,
        DevConfig::evalGrid(),
        objectiveKindFromConfig(),
        opts,
        latencyWeight,
        &tickBudget
    );

    std::string rec;
    if (DevSession::get().isRecording()) {
        rec = DevSession::get().statusLine();
    }
    out = DevReport::fromGrid(grid, std::move(rec));
    if (out.valid && planEv.plannedDelay > 0
        && (!out.hasBestSurviving || out.bestSurvivingDelay < 0)) {
        out.liveVsEvalMismatch = true;
        out.livePlannedDelay = planEv.plannedDelay;
        out.overlayLine = DevReport::formatOverlayLine(out);
    }
    return out.valid;
}
} // namespace

DevReport DevReport::fromGrid(
    DevEvalEngine::GridReport const& grid,
    std::string recLine
) {
    DevReport r;
    r.recLine = std::move(recLine);
    r.fidelity = grid.fidelity.fidelity;
    r.claimsAllowed = grid.fidelity.claimsAllowed;
    r.approx = grid.fidelity.approx;
    r.objectiveId = grid.objective.objectiveId
        ? grid.objective.objectiveId
        : "survive_then_endx_v1";
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
    }
    else {
        r.hasIdealDelay = false;
        r.idealDelay = -1;
        r.idealBlockedReason = grid.objective.idealBlockedReason;
    }

    r.overlayLine = formatOverlayLine(r);

    matjson::Value j = matjson::Value::object();
    if (!r.recLine.empty()) j["rec"] = r.recLine;
    j["fidelity"] = DevStateDiff::fidelityId(r.fidelity);
    j["claimsAllowed"] = r.claimsAllowed;
    j["approx"] = r.approx;
    j["objectiveId"] = r.objectiveId;
    j["horizonTicks"] = r.horizonTicks;
    j["tailTicks"] = r.tailTicks;
    if (r.claimsAllowed && r.hasBestSurviving) {
        j["bestSurvivingDelay"] = r.bestSurvivingDelay;
    }
    if (r.claimsAllowed && r.hasBestEvaluated) {
        j["bestEvaluatedDelay"] = r.bestEvaluatedDelay;
    }
    j["idealEligible"] = r.idealEligible;
    if (r.idealEligible && r.hasIdealDelay) {
        j["idealDelay"] = r.idealDelay;
    }
    else if (!r.idealBlockedReason.empty()) {
        j["idealBlockedReason"] = r.idealBlockedReason;
    }
    if (!grid.fidelity.evalSuppressedReason.empty()) {
        j["evalSuppressedReason"] = grid.fidelity.evalSuppressedReason;
    }
    j["overlay"] = r.overlayLine;
    if (r.liveVsEvalMismatch) {
        j["liveVsEvalMismatch"] = true;
        j["livePlannedDelay"] = r.livePlannedDelay;
    }
    r.jsonLine = j.dump(0);
    r.valid = true;
    return r;
}

std::string DevReport::formatOverlayLine(DevReport const& r) {
    std::string line;
    if (!r.recLine.empty()) {
        line = r.recLine;
    }

    auto append = [&](std::string const& bit) {
        if (bit.empty()) return;
        if (!line.empty()) line += " | ";
        line += bit;
    };

    append(fmt::format("fid={}", fidShort(r.fidelity, r.approx)));

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
    }
    else {
        append("bestSurv=none");
    }
    if (r.liveVsEvalMismatch) {
        append(fmt::format("mismatch liveDelay={}", r.livePlannedDelay));
    }

    if (r.hasBestEvaluated) {
        append(fmt::format(
            "bestEval={} obj={}", r.bestEvaluatedDelay, r.objectiveId
        ));
    }
    else {
        append(fmt::format("bestEval=none obj={}", r.objectiveId));
    }

    if (r.idealEligible && r.hasIdealDelay) {
        append(fmt::format("ideal={}", r.idealDelay));
    }
    return line;
}

void DevReport::setLast(DevReport report) {
    g_lastReport = std::move(report);
    g_hasLastReport = g_lastReport.valid;
}

DevReport const& DevReport::last() {
    return g_lastReport;
}

bool DevReport::hasLast() {
    return g_hasLastReport && g_lastReport.valid;
}

void DevReport::clearLast() {
    g_lastReport = {};
    g_hasLastReport = false;
}

bool DevReport::evalLastAttempt(std::string& err) {
    err.clear();
    auto* layer = PlayLayer::get();
    if (!layer) {
        err = "Enter a level to run eval.";
        return false;
    }

    auto& store = DevTraceStore::get();
    DevAttempt const* target = nullptr;

    if (store.isOpen()) {
        auto const& attempts = store.session().attempts;
        for (auto it = attempts.rbegin(); it != attempts.rend(); ++it) {
            if (findPlanFinished(*it)) {
                target = &*it;
                break;
            }
        }
    }
    if (!target) {
        err = "No attempt with a planFinished witness in the open session.";
        return false;
    }
    auto const* planEv = findPlanFinished(*target);
    if (!planEv) {
        err = "No planFinished witness on attempt.";
        return false;
    }

    DevReport rep;
    if (!runEvalOnPlanEvent(layer, *planEv, rep, err)) {
        if (err.empty()) err = "eval-failed";
        return false;
    }
    setLast(std::move(rep));
    return true;
}

bool DevReport::evalSession(std::string& err, int* outCount) {
    err.clear();
    if (outCount) *outCount = 0;
    auto* layer = PlayLayer::get();
    if (!layer) {
        err = "Enter a level to run eval.";
        return false;
    }
    auto& store = DevTraceStore::get();
    if (!store.isOpen()) {
        err = "No open Dev session.";
        return false;
    }

    int ran = 0;
    DevReport lastOk;
    for (auto const& attempt : store.session().attempts) {
        if (!attempt.sealed && &attempt != &store.session().attempts.back()) {
            continue;
        }
        auto const* planEv = findPlanFinished(attempt);
        if (!planEv) continue;
        DevReport rep;
        std::string localErr;
        if (!runEvalOnPlanEvent(layer, *planEv, rep, localErr)) continue;
        lastOk = std::move(rep);
        ++ran;
    }
    if (ran == 0) {
        err = "No evaluable planFinished events in session.";
        return false;
    }
    setLast(std::move(lastOk));
    if (outCount) *outCount = ran;
    return true;
}

bool DevReport::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    // Synthetic: survive objective → no ideal= in overlay.
    {
        DevReport r;
        r.valid = true;
        r.claimsAllowed = true;
        r.fidelity = DevFidelity::Reproduced;
        r.objectiveId = "survive_then_endx_v1";
        r.hasBestSurviving = true;
        r.bestSurvivingDelay = 2;
        r.hasBestEvaluated = true;
        r.bestEvaluatedDelay = 2;
        r.idealEligible = false;
        r.hasIdealDelay = false;
        r.idealBlockedReason = "objective_not_horizon_clear";
        // Corrupt path must not emit ideal even if hasIdealDelay wrongly set.
        r.hasIdealDelay = true;
        r.idealDelay = 99;
        auto line = formatOverlayLine(r);
        expect(line.find("ideal=") == std::string::npos, "no-ideal-survive");
        expect(line.find("bestSurv=2") != std::string::npos, "bestsurv");
        expect(line.find("obj=survive_then_endx_v1") != std::string::npos, "obj");
        expect(line.find("fid=reproduced") != std::string::npos, "fid");
    }

    // Eligible horizon_clear → ideal= present.
    {
        DevReport r;
        r.claimsAllowed = true;
        r.fidelity = DevFidelity::Reproduced;
        r.objectiveId = "horizon_clear_v1";
        r.hasBestSurviving = true;
        r.bestSurvivingDelay = 1;
        r.hasBestEvaluated = true;
        r.bestEvaluatedDelay = 1;
        r.idealEligible = true;
        r.hasIdealDelay = true;
        r.idealDelay = 1;
        auto line = formatOverlayLine(r);
        expect(line.find("ideal=1") != std::string::npos, "ideal-ok");
    }

    // fromGrid must strip ideal when not eligible even if Result has numbers.
    {
        DevEvalEngine::GridReport gr;
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
        auto r = fromGrid(gr, "REC 1/10");
        expect(r.valid, "from-valid");
        expect(!r.hasIdealDelay, "from-no-ideal-flag");
        expect(r.overlayLine.find("ideal=") == std::string::npos, "from-no-ideal");
        expect(r.jsonLine.find("\"idealDelay\"") == std::string::npos, "from-json-no-ideal");
        expect(r.overlayLine.find("REC 1/10") != std::string::npos, "from-rec");
    }

    // Claims off → bestSurv=none, no ideal.
    {
        DevReport r;
        r.claimsAllowed = false;
        r.fidelity = DevFidelity::NotReproducible;
        r.objectiveId = "horizon_clear_v1";
        r.idealEligible = true;
        r.hasIdealDelay = true;
        r.idealDelay = 3;
        auto line = formatOverlayLine(r);
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
