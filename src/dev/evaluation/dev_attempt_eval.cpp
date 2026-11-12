#include "dev_attempt_eval.hpp"
#include "../dev_config.hpp"
#include "../reporting/dev_report.hpp"

EbDevAttemptEval EbDevAttemptEval::fromGrid(EbDevEvalEngine::GridReport const& grid,
                                            EbDevReport const& report) {
    EbDevAttemptEval out;
    out.valid = report.valid;
    if (!out.valid)
        return out;

    out.timeBasis = grid.timeBasis;
    out.clockSchema = grid.clockSchema;
    out.liveSimPathId = grid.liveSimPathId;
    out.evalSimPathId = grid.evalSimPathId;
    out.clockMismatch = grid.clockMismatch;

    out.fidelity = grid.fidelity.fidelity;
    out.claimsAllowed = grid.fidelity.claimsAllowed;
    out.approx = grid.fidelity.approx;
    out.evalSuppressedReason = grid.fidelity.evalSuppressedReason;

    out.objectiveId = report.objectiveId;
    out.horizonTicks = grid.horizonTicks;
    out.tailTicks = grid.tailTicks;
    out.gridId = EbDevConfig::evalGridId(grid.grid);
    out.limit = grid.limit;
    out.objectivePartial = grid.objectivePartial;

    out.hasBestSurviving = report.hasBestSurviving;
    out.bestSurvivingDelay = report.bestSurvivingDelay;
    out.hasBestEvaluated = report.hasBestEvaluated;
    out.bestEvaluatedDelay = report.bestEvaluatedDelay;

    out.idealEligible = report.idealEligible;
    out.hasIdealDelay = report.hasIdealDelay;
    out.idealDelay = report.idealDelay;
    out.idealBlockedReason = report.idealBlockedReason;

    out.plannedDelay = report.plannedDelay;
    out.liveVsEvalMismatch = report.liveVsEvalMismatch;
    out.livePlannedDelay = report.livePlannedDelay;
    if (report.simNetClass)
        out.simNetClass = report.simNetClass;

    out.overlayLine = report.overlayLine;
    out.samples = grid.samples;
    return out;
}

matjson::Value EbDevAttemptEval::toJson(EbDevAttemptEval const& eval) {
    matjson::Value root = matjson::Value::object();
    if (!eval.valid)
        return root;

    root["valid"] = true;
    if (!eval.timeBasis.empty())
        root["timeBasis"] = eval.timeBasis;
    root["clockSchema"] = eval.clockSchema;
    if (!eval.liveSimPathId.empty())
        root["liveSimPathId"] = eval.liveSimPathId;
    if (!eval.evalSimPathId.empty())
        root["evalSimPathId"] = eval.evalSimPathId;
    root["clockMismatch"] = eval.clockMismatch;

    root["fidelity"] = EbDevStateDiff::fidelityId(eval.fidelity);
    root["claimsAllowed"] = eval.claimsAllowed;
    root["approx"] = eval.approx;
    if (!eval.evalSuppressedReason.empty())
        root["evalSuppressedReason"] = eval.evalSuppressedReason;

    root["objectiveId"] = eval.objectiveId;
    root["horizonTicks"] = eval.horizonTicks;
    root["tailTicks"] = eval.tailTicks;
    root["grid"] = eval.gridId;
    root["limit"] = eval.limit;
    root["objectivePartial"] = eval.objectivePartial;

    if (eval.claimsAllowed && eval.hasBestSurviving)
        root["bestSurvivingDelay"] = eval.bestSurvivingDelay;
    if (eval.claimsAllowed && eval.hasBestEvaluated)
        root["bestEvaluatedDelay"] = eval.bestEvaluatedDelay;

    root["idealEligible"] = eval.idealEligible;
    if (eval.idealEligible && eval.hasIdealDelay) {
        root["idealDelay"] = eval.idealDelay;
    } else if (!eval.idealBlockedReason.empty()) {
        root["idealBlockedReason"] = eval.idealBlockedReason;
    }

    root["plannedDelay"] = eval.plannedDelay;
    if (eval.liveVsEvalMismatch) {
        root["liveVsEvalMismatch"] = true;
        root["livePlannedDelay"] = eval.livePlannedDelay;
    }
    if (!eval.simNetClass.empty())
        root["simNetClass"] = eval.simNetClass;
    if (!eval.overlayLine.empty())
        root["overlay"] = eval.overlayLine;

    matjson::Value samples = matjson::Value::array();
    for (auto const& s : eval.samples) {
        matjson::Value sj = matjson::Value::object();
        sj["delay"] = s.delay;
        sj["survivedPrimary"] = s.survivedPrimary;
        sj["deathTick"] = s.deathTick;
        sj["endX"] = s.endX;
        sj["unknown"] = s.unknown;
        sj["budgetHit"] = s.budgetHit;
        sj["aborted"] = s.aborted;
        sj["tailFailed"] = s.tailFailed;
        sj["tailDeathTick"] = s.tailDeathTick;
        sj["tailEndX"] = s.tailEndX;
        sj["tailComplete"] = s.tailComplete;
        sj["threatCleared"] = s.threatCleared;
        sj["ticksRan"] = s.ticksRan;
        samples.push(sj);
    }
    root["samples"] = samples;
    return root;
}

bool EbDevAttemptEval::fromJson(matjson::Value const& j, EbDevAttemptEval& out, std::string& err) {
    err.clear();
    out = {};
    if (!j.isObject()) {
        err = "eval-not-object";
        return false;
    }
    out.valid = j.contains("valid") ? j["valid"].asBool().unwrapOr(false) : true;
    if (!out.valid)
        return true;

    out.timeBasis =
        j.contains("timeBasis") ? std::string(j["timeBasis"].asString().unwrapOr("")) : "";
    out.clockSchema =
        j.contains("clockSchema") ? static_cast<int>(j["clockSchema"].asInt().unwrapOr(0)) : 0;
    out.liveSimPathId =
        j.contains("liveSimPathId") ? std::string(j["liveSimPathId"].asString().unwrapOr("")) : "";
    out.evalSimPathId =
        j.contains("evalSimPathId") ? std::string(j["evalSimPathId"].asString().unwrapOr("")) : "";
    out.clockMismatch =
        j.contains("clockMismatch") ? j["clockMismatch"].asBool().unwrapOr(false) : false;

    if (j.contains("fidelity")) {
        auto const fid = std::string(j["fidelity"].asString().unwrapOr(""));
        if (!EbDevStateDiff::parseFidelity(fid, out.fidelity)) {
            err = "bad-eval-fidelity";
            return false;
        }
    }
    out.claimsAllowed =
        j.contains("claimsAllowed") ? j["claimsAllowed"].asBool().unwrapOr(false) : false;
    out.approx = j.contains("approx") ? j["approx"].asBool().unwrapOr(false) : false;
    out.evalSuppressedReason = j.contains("evalSuppressedReason")
                                   ? std::string(j["evalSuppressedReason"].asString().unwrapOr(""))
                                   : "";

    out.objectiveId =
        j.contains("objectiveId") ? std::string(j["objectiveId"].asString().unwrapOr("")) : "";
    out.horizonTicks =
        j.contains("horizonTicks") ? static_cast<int>(j["horizonTicks"].asInt().unwrapOr(0)) : 0;
    out.tailTicks =
        j.contains("tailTicks") ? static_cast<int>(j["tailTicks"].asInt().unwrapOr(0)) : 0;
    out.gridId = j.contains("grid") ? std::string(j["grid"].asString().unwrapOr("")) : "";
    out.limit = j.contains("limit") ? static_cast<int>(j["limit"].asInt().unwrapOr(0)) : 0;
    out.objectivePartial =
        j.contains("objectivePartial") ? j["objectivePartial"].asBool().unwrapOr(false) : false;

    if (j.contains("bestSurvivingDelay")) {
        out.hasBestSurviving = true;
        out.bestSurvivingDelay = static_cast<int>(j["bestSurvivingDelay"].asInt().unwrapOr(-1));
    }
    if (j.contains("bestEvaluatedDelay")) {
        out.hasBestEvaluated = true;
        out.bestEvaluatedDelay = static_cast<int>(j["bestEvaluatedDelay"].asInt().unwrapOr(-1));
    }

    out.idealEligible =
        j.contains("idealEligible") ? j["idealEligible"].asBool().unwrapOr(false) : false;
    if (j.contains("idealDelay")) {
        out.hasIdealDelay = true;
        out.idealDelay = static_cast<int>(j["idealDelay"].asInt().unwrapOr(-1));
    }
    out.idealBlockedReason = j.contains("idealBlockedReason")
                                 ? std::string(j["idealBlockedReason"].asString().unwrapOr(""))
                                 : "";

    out.plannedDelay =
        j.contains("plannedDelay") ? static_cast<int>(j["plannedDelay"].asInt().unwrapOr(-1)) : -1;
    out.liveVsEvalMismatch =
        j.contains("liveVsEvalMismatch") ? j["liveVsEvalMismatch"].asBool().unwrapOr(false) : false;
    out.livePlannedDelay = j.contains("livePlannedDelay")
                               ? static_cast<int>(j["livePlannedDelay"].asInt().unwrapOr(-1))
                               : -1;
    out.simNetClass =
        j.contains("simNetClass") ? std::string(j["simNetClass"].asString().unwrapOr("")) : "";
    out.overlayLine =
        j.contains("overlay") ? std::string(j["overlay"].asString().unwrapOr("")) : "";

    if (j.contains("samples") && j["samples"].isArray()) {
        for (auto const& sj : j["samples"].asArray().unwrap()) {
            EbDevObjective::Sample s;
            s.delay = sj.contains("delay") ? static_cast<int>(sj["delay"].asInt().unwrapOr(0)) : 0;
            s.survivedPrimary =
                sj.contains("survivedPrimary") ? sj["survivedPrimary"].asBool().unwrapOr(false)
                                               : false;
            s.deathTick =
                sj.contains("deathTick") ? static_cast<int>(sj["deathTick"].asInt().unwrapOr(-1))
                                         : -1;
            s.endX = sj.contains("endX") ? static_cast<float>(sj["endX"].asDouble().unwrapOr(0.0))
                                         : 0.f;
            s.unknown = sj.contains("unknown") ? sj["unknown"].asBool().unwrapOr(false) : false;
            s.budgetHit =
                sj.contains("budgetHit") ? sj["budgetHit"].asBool().unwrapOr(false) : false;
            s.aborted = sj.contains("aborted") ? sj["aborted"].asBool().unwrapOr(false) : false;
            s.tailFailed =
                sj.contains("tailFailed") ? sj["tailFailed"].asBool().unwrapOr(false) : false;
            s.tailDeathTick = sj.contains("tailDeathTick")
                                  ? static_cast<int>(sj["tailDeathTick"].asInt().unwrapOr(-1))
                                  : -1;
            s.tailEndX = sj.contains("tailEndX")
                             ? static_cast<float>(sj["tailEndX"].asDouble().unwrapOr(0.0))
                             : 0.f;
            s.tailComplete =
                sj.contains("tailComplete") ? sj["tailComplete"].asBool().unwrapOr(false) : false;
            s.threatCleared =
                sj.contains("threatCleared") ? sj["threatCleared"].asBool().unwrapOr(false) : false;
            s.ticksRan =
                sj.contains("ticksRan") ? static_cast<int>(sj["ticksRan"].asInt().unwrapOr(0)) : 0;
            out.samples.push_back(s);
        }
    }
    return true;
}
