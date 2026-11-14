#include "dev_attempt_eval.hpp"
#include "../dev_config.hpp"
#include "../reporting/dev_report.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include <algorithm>
#include <unordered_set>

class EbDevAttemptEvalTraj {
public:
    static constexpr int kMaxPoints = 96;

    static char const* coarseSurvivalClass(bool alive, bool unknown) {
        if (unknown)
            return "";
        if (!alive)
            return "lethal";
        return "alive";
    }

    static std::vector<EbDevTrajPoint>
    fromRaw(std::vector<EbShadowSimulator::TrajSample> const& raw) {
        std::vector<EbDevTrajPoint> points;
        if (raw.empty())
            return points;

        int stride = 1;
        int const count = static_cast<int>(raw.size());
        if (count > kMaxPoints)
            stride = (count + kMaxPoints - 1) / kMaxPoints;

        points.reserve(static_cast<size_t>((count + stride - 1) / stride));
        for (int index = 0; index < count; index += stride) {
            auto const& sample = raw[static_cast<size_t>(index)];
            EbDevTrajPoint point;
            point.tick = sample.tick;
            point.x = sample.x;
            point.y = sample.y;
            point.vy = sample.vy;
            point.alive = sample.alive;
            point.unknown = sample.unknown;
            point.survivalClass = coarseSurvivalClass(sample.alive, sample.unknown);
            points.push_back(std::move(point));
        }
        // Always keep the last sample if stride skipped it.
        if (stride > 1 && (count - 1) % stride != 0) {
            auto const& sample = raw.back();
            EbDevTrajPoint point;
            point.tick = sample.tick;
            point.x = sample.x;
            point.y = sample.y;
            point.vy = sample.vy;
            point.alive = sample.alive;
            point.unknown = sample.unknown;
            point.survivalClass = coarseSurvivalClass(sample.alive, sample.unknown);
            points.push_back(std::move(point));
        }
        return points;
    }

    static void pushUnique(std::vector<std::pair<std::string, int>>& wants, char const* role,
                           int delay, std::unordered_set<int>& seen) {
        if (!role || delay < 0)
            return;
        if (!seen.insert(delay).second)
            return;
        wants.emplace_back(role, delay);
    }

private:
    EbDevAttemptEvalTraj() = delete;
};

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

void EbDevAttemptEval::captureOptionalTrajectories(EbDevAttemptEval& eval, PlayLayer* layer,
                                                   EbDevLiveWitness const& witness, bool down) {
    eval.trajectories.clear();
    if (!eval.valid || !EbDevConfig::recordTraj())
        return;
    if (!layer || !witness.valid)
        return;

    std::vector<std::pair<std::string, int>> wants;
    std::unordered_set<int> seen;
    // Prefer chosen; skip duplicates when bestSurv/bestEval share the delay.
    EbDevAttemptEvalTraj::pushUnique(wants, "chosen", eval.plannedDelay, seen);
    if (eval.claimsAllowed && eval.hasBestSurviving) {
        EbDevAttemptEvalTraj::pushUnique(wants, "bestSurv", eval.bestSurvivingDelay, seen);
    }
    if (eval.claimsAllowed && eval.hasBestEvaluated) {
        EbDevAttemptEvalTraj::pushUnique(wants, "bestEval", eval.bestEvaluatedDelay, seen);
    }
    if (wants.empty())
        return;

    int const schedHorizon = eval.horizonTicks + std::max(0, eval.tailTicks);
    if (schedHorizon <= 0)
        return;

    EbShadowSimulator::get().clearEvalAbort();
    for (auto const& want : wants) {
        if (EbShadowSimulator::get().evalAbortRequested())
            break;
        auto const evaluated =
            std::find_if(eval.samples.begin(), eval.samples.end(),
                         [&](auto const& sample) { return sample.delay == want.second; });
        float const phase = evaluated == eval.samples.end() ? 0.f : evaluated->firePhase;
        auto raw = EbShadowSimulator::get().evalDelayFromFreeze(
            layer, witness, want.second, schedHorizon, down, nullptr, true, phase);
        if (raw.clockMismatch || !raw.ran)
            continue;

        EbDevTrajCapture capture;
        capture.role = want.first;
        capture.delay = want.second;
        capture.points = EbDevAttemptEvalTraj::fromRaw(raw.traj);
        if (raw.unknown)
            capture.survivalClass.clear();
        else if (raw.died)
            capture.survivalClass = "lethal";
        else
            capture.survivalClass = "alive";
        if (!capture.points.empty())
            eval.trajectories.push_back(std::move(capture));
    }
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
        sj["firePhase"] = s.firePhase;
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

    if (!eval.trajectories.empty()) {
        matjson::Value trajs = matjson::Value::array();
        for (auto const& traj : eval.trajectories) {
            matjson::Value tj = matjson::Value::object();
            if (!traj.role.empty())
                tj["role"] = traj.role;
            tj["delay"] = traj.delay;
            if (!traj.survivalClass.empty())
                tj["survivalClass"] = traj.survivalClass;
            matjson::Value pts = matjson::Value::array();
            for (auto const& point : traj.points) {
                matjson::Value pj = matjson::Value::object();
                pj["tick"] = point.tick;
                pj["x"] = point.x;
                pj["y"] = point.y;
                pj["vy"] = point.vy;
                pj["alive"] = point.alive;
                pj["unknown"] = point.unknown;
                if (!point.survivalClass.empty())
                    pj["survivalClass"] = point.survivalClass;
                pts.push(pj);
            }
            tj["points"] = pts;
            trajs.push(tj);
        }
        root["trajectories"] = trajs;
    }
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
            s.firePhase = sj.contains("firePhase")
                              ? static_cast<float>(sj["firePhase"].asDouble().unwrapOr(0.0))
                              : 0.f;
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

    if (j.contains("trajectories") && j["trajectories"].isArray()) {
        for (auto const& tj : j["trajectories"].asArray().unwrap()) {
            if (!tj.isObject())
                continue;
            EbDevTrajCapture capture;
            capture.role =
                tj.contains("role") ? std::string(tj["role"].asString().unwrapOr("")) : "";
            capture.delay =
                tj.contains("delay") ? static_cast<int>(tj["delay"].asInt().unwrapOr(-1)) : -1;
            capture.survivalClass = tj.contains("survivalClass")
                                        ? std::string(tj["survivalClass"].asString().unwrapOr(""))
                                        : "";
            if (tj.contains("points") && tj["points"].isArray()) {
                for (auto const& pj : tj["points"].asArray().unwrap()) {
                    if (!pj.isObject())
                        continue;
                    EbDevTrajPoint point;
                    point.tick =
                        pj.contains("tick") ? static_cast<int>(pj["tick"].asInt().unwrapOr(0)) : 0;
                    point.x =
                        pj.contains("x") ? static_cast<float>(pj["x"].asDouble().unwrapOr(0.0))
                                         : 0.f;
                    point.y =
                        pj.contains("y") ? static_cast<float>(pj["y"].asDouble().unwrapOr(0.0))
                                         : 0.f;
                    point.vy =
                        pj.contains("vy") ? static_cast<float>(pj["vy"].asDouble().unwrapOr(0.0))
                                          : 0.f;
                    point.alive =
                        pj.contains("alive") ? pj["alive"].asBool().unwrapOr(true) : true;
                    point.unknown =
                        pj.contains("unknown") ? pj["unknown"].asBool().unwrapOr(false) : false;
                    point.survivalClass =
                        pj.contains("survivalClass")
                            ? std::string(pj["survivalClass"].asString().unwrapOr(""))
                            : "";
                    capture.points.push_back(std::move(point));
                }
            }
            out.trajectories.push_back(std::move(capture));
        }
    }
    return true;
}
