#include "dev_event_codec.hpp"
#include "../../evaluation/dev_reject_ledger.hpp"
#include "../../evaluation/dev_state_diff.hpp"
#include "../dev_live_witness.hpp"
#include "../dev_trace_store.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <cstdio>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

matjson::Value EbDevEventCodec::eventToJson(EbDevDecisionEvent const& e) {
    matjson::Value otherIndex = matjson::Value::object();
    otherIndex["eventId"] = e.eventId;
    otherIndex["attemptId"] = e.attemptId;
    otherIndex["pressId"] = e.pressId;
    otherIndex["cycleId"] = e.cycleId;
    otherIndex["planId"] = e.planId;
    otherIndex["parentEventId"] = e.parentEventId;
    otherIndex["playerIndex"] = e.playerIndex;
    otherIndex["down"] = e.down;
    otherIndex["source"] = EbDevTraceStore::sourceId(e.source);
    otherIndex["kind"] = EbDevTraceStore::kindId(e.kind);
    otherIndex["effectTick"] = e.effectTick;
    otherIndex["pressTick"] = e.pressTick;
    otherIndex["plannedDelay"] = e.plannedDelay;
    otherIndex["plannedFireTick"] = e.plannedFireTick;
    otherIndex["actualFireTick"] = e.actualFireTick;
    if (!e.assistReason.empty())
        otherIndex["assistReason"] = e.assistReason;
    if (!e.endReason.empty())
        otherIndex["endReason"] = e.endReason;
    if (e.hasAssistDiag) {
        otherIndex["armVerifyOk"] = e.armVerifyOk;
        otherIndex["budgetHitAtArm"] = e.budgetHitAtArm;
        otherIndex["recheckDelta"] = e.recheckDelta;
        otherIndex["holdExtendMax"] = e.holdExtendMax;
        otherIndex["deathAfterFire"] = e.deathAfterFire;
        otherIndex["ticksToDeath"] = e.ticksToDeath;
        otherIndex["liveVsEvalMismatch"] = e.liveVsEvalMismatch;
    }
    if (e.hasClockStamp && e.clockStamp.valid) {
        otherIndex["clock"] = EbDevTraceStoreImplementation::clockStampToJson(e.clockStamp);
    }
    if (e.hasWitness && e.witness.valid) {
        otherIndex["witness"] = EbDevTraceStoreImplementation::witnessToJson(e.witness);
    }
    if (e.hasFidelity) {
        otherIndex["fidelity"] = EbDevStateDiff::fidelityId(e.fidelity);
        otherIndex["claimsAllowed"] = e.claimsAllowed;
        otherIndex["approx"] = e.approx;
        if (!e.evalSuppressedReason.empty()) {
            otherIndex["evalSuppressedReason"] = e.evalSuppressedReason;
        }
        if (!e.diffs.empty()) {
            std::vector<matjson::Value> dj;
            dj.reserve(e.diffs.size());
            for (auto const& d : e.diffs) {
                matjson::Value item = matjson::Value::object();
                item["category"] = EbDevLiveWitness::categoryId(d.category);
                item["fieldOrObjectId"] = d.fieldOrObjectId;
                item["liveValue"] = d.liveValue;
                item["restoredValue"] = d.restoredValue;
                item["kind"] = EbDevStateDiff::diffKindId(d.kind);
                dj.push_back(item);
            }
            otherIndex["diffs"] = dj;
        }
    }
    if (!e.candNotes.empty() || e.candNotesTruncated) {
        std::vector<matjson::Value> notes;
        notes.reserve(e.candNotes.size());
        for (auto const& n : e.candNotes) {
            matjson::Value nj = matjson::Value::object();
            nj["delay"] = n.delay;
            nj["status"] = EbDevRejectLedger::statusId(n.status);
            nj["rejectCode"] = EbDevRejectLedger::rejectCodeId(n.rejectCode);
            if (!n.detail.empty())
                nj["detail"] = n.detail;
            if (n.hasScores) {
                nj["surviveBin"] = n.surviveBin;
                nj["endX"] = n.endX;
                nj["latencyTerm"] = n.latencyTerm;
                nj["threatCleared"] = n.threatCleared;
                nj["score"] = n.score;
            }
            notes.push_back(nj);
        }
        otherIndex["candNotes"] = notes;
        if (e.candNotesTruncated)
            otherIndex["candNotesTruncated"] = true;
    }
    if (!e.attribution.empty()) {
        std::vector<matjson::Value> attrs;
        attrs.reserve(e.attribution.size());
        for (auto const& a : e.attribution) {
            matjson::Value aj = matjson::Value::object();
            aj["code"] = EbDevRejectLedger::attributionId(a.code);
            if (!a.detail.empty())
                aj["detail"] = a.detail;
            attrs.push_back(aj);
        }
        otherIndex["attribution"] = attrs;
    }
    if (e.hasPlanCost) {
        otherIndex["planUs"] = e.planUs;
        otherIndex["budgetHit"] = e.budgetHit;
        otherIndex["candidatesTried"] = e.candidatesTried;
        otherIndex["adaptiveShell"] = e.adaptiveShell;
        otherIndex["adaptiveFellThrough"] = e.adaptiveFellThrough;
    }
    return otherIndex;
}

bool EbDevEventCodec::eventFromJson(matjson::Value const& j, EbDevDecisionEvent& e, std::string& err) {
    e = {};
    e.eventId = j.contains("eventId") ? j["eventId"].asInt().unwrapOr(0) : 0;
    e.attemptId = j.contains("attemptId") ? j["attemptId"].asInt().unwrapOr(0) : 0;
    e.pressId = j.contains("pressId") ? j["pressId"].asInt().unwrapOr(0) : 0;
    e.cycleId = j.contains("cycleId") ? j["cycleId"].asInt().unwrapOr(0) : 0;
    e.planId = j.contains("planId") ? j["planId"].asInt().unwrapOr(0) : 0;
    e.parentEventId = j.contains("parentEventId") ? j["parentEventId"].asInt().unwrapOr(0) : 0;
    e.playerIndex =
        j.contains("playerIndex") ? static_cast<int>(j["playerIndex"].asInt().unwrapOr(0)) : 0;
    e.down = j.contains("down") ? j["down"].asBool().unwrapOr(true) : true;
    auto sourceValue =
        j.contains("source") ? std::string(j["source"].asString().unwrapOr("live")) : "live";
    if (!EbDevTraceStore::parseSource(sourceValue, e.source)) {
        err = "bad-source";
        return false;
    }
    auto kind = j.contains("kind") ? std::string(j["kind"].asString().unwrapOr("")) : "";
    if (!EbDevTraceStore::parseKind(kind, e.kind)) {
        err = "bad-kind";
        return false;
    }
    e.effectTick =
        j.contains("effectTick") ? static_cast<int>(j["effectTick"].asInt().unwrapOr(0)) : 0;
    e.pressTick =
        j.contains("pressTick") ? static_cast<int>(j["pressTick"].asInt().unwrapOr(0)) : 0;
    e.plannedDelay =
        j.contains("plannedDelay") ? static_cast<int>(j["plannedDelay"].asInt().unwrapOr(0)) : 0;
    e.plannedFireTick = j.contains("plannedFireTick")
                            ? static_cast<int>(j["plannedFireTick"].asInt().unwrapOr(-1))
                            : -1;
    e.actualFireTick = j.contains("actualFireTick")
                           ? static_cast<int>(j["actualFireTick"].asInt().unwrapOr(-1))
                           : -1;
    if (j.contains("assistReason")) {
        e.assistReason = std::string(j["assistReason"].asString().unwrapOr(""));
    }
    if (j.contains("endReason")) {
        e.endReason = std::string(j["endReason"].asString().unwrapOr(""));
    }
    if (j.contains("armVerifyOk") || j.contains("deathAfterFire") || j.contains("budgetHitAtArm") ||
        j.contains("recheckDelta") || j.contains("holdExtendMax") ||
        j.contains("liveVsEvalMismatch")) {
        e.hasAssistDiag = true;
        e.armVerifyOk = j.contains("armVerifyOk") ? j["armVerifyOk"].asBool().unwrapOr(true) : true;
        e.budgetHitAtArm =
            j.contains("budgetHitAtArm") ? j["budgetHitAtArm"].asBool().unwrapOr(false) : false;
        e.recheckDelta = j.contains("recheckDelta")
                             ? static_cast<int>(j["recheckDelta"].asInt().unwrapOr(0))
                             : 0;
        e.holdExtendMax = j.contains("holdExtendMax")
                              ? static_cast<int>(j["holdExtendMax"].asInt().unwrapOr(-1))
                              : -1;
        e.deathAfterFire =
            j.contains("deathAfterFire") ? j["deathAfterFire"].asBool().unwrapOr(false) : false;
        e.ticksToDeath = j.contains("ticksToDeath")
                             ? static_cast<int>(j["ticksToDeath"].asInt().unwrapOr(-1))
                             : -1;
        e.liveVsEvalMismatch = j.contains("liveVsEvalMismatch")
                                   ? j["liveVsEvalMismatch"].asBool().unwrapOr(false)
                                   : false;
    }
    if (j.contains("clock")) {
        std::string clockErr;
        if (!EbDevTraceStoreImplementation::clockStampFromJson(j["clock"], e.clockStamp, clockErr)) {
            err = clockErr.empty() ? "bad-clock" : clockErr;
            return false;
        }
        e.hasClockStamp = true;
    }
    if (j.contains("witness")) {
        std::string witnessErr;
        if (!EbDevTraceStoreImplementation::witnessFromJson(j["witness"], e.witness, witnessErr)) {
            err = witnessErr.empty() ? "bad-witness" : witnessErr;
            return false;
        }
        e.hasWitness = true;
    }
    if (j.contains("fidelity")) {
        e.hasFidelity = true;
        auto fid = std::string(j["fidelity"].asString().unwrapOr(""));
        if (!EbDevStateDiff::parseFidelity(fid, e.fidelity)) {
            err = "bad-fidelity";
            return false;
        }
        e.claimsAllowed =
            j.contains("claimsAllowed") ? j["claimsAllowed"].asBool().unwrapOr(false) : false;
        e.approx = j.contains("approx") ? j["approx"].asBool().unwrapOr(false) : false;
        if (j.contains("evalSuppressedReason")) {
            e.evalSuppressedReason = std::string(j["evalSuppressedReason"].asString().unwrapOr(""));
        }
        if (j.contains("diffs") && j["diffs"].isArray()) {
            for (auto const& item : j["diffs"].asArray().unwrap()) {
                EbDevDiffEntry d;
                auto categoryValue = std::string(item["category"].asString().unwrapOr(""));
                if (!EbDevLiveWitness::parseCategory(categoryValue, d.category)) {
                    err = "bad-diff-category";
                    return false;
                }
                d.fieldOrObjectId = std::string(item["fieldOrObjectId"].asString().unwrapOr(""));
                d.liveValue = std::string(item["liveValue"].asString().unwrapOr(""));
                d.restoredValue = std::string(item["restoredValue"].asString().unwrapOr(""));
                auto kind = std::string(item["kind"].asString().unwrapOr("mismatch"));
                if (kind == "unrestored")
                    d.kind = DevDiffKind::Unrestored;
                else if (kind == "digestOnly")
                    d.kind = DevDiffKind::DigestOnly;
                else
                    d.kind = DevDiffKind::Mismatch;
                e.diffs.push_back(d);
            }
        }
    }
    if (j.contains("candNotes") && j["candNotes"].isArray()) {
        for (auto const& nj : j["candNotes"].asArray().unwrap()) {
            EbDevRejectLedger::CandNote n;
            n.delay = static_cast<int>(nj["delay"].asInt().unwrapOr(0));
            auto state = std::string(nj["status"].asString().unwrapOr("skipped"));
            auto rc = std::string(nj["rejectCode"].asString().unwrapOr("not-viable"));
            if (!EbDevRejectLedger::parseStatus(state, n.status)) {
                err = "bad-cand-status";
                return false;
            }
            if (!EbDevRejectLedger::parseRejectCode(rc, n.rejectCode)) {
                err = "bad-reject-code";
                return false;
            }
            if (nj.contains("detail")) {
                n.detail = std::string(nj["detail"].asString().unwrapOr(""));
            }
            if (nj.contains("surviveBin") || nj.contains("endX") || nj.contains("score")) {
                n.hasScores = true;
                n.surviveBin = static_cast<int>(nj["surviveBin"].asInt().unwrapOr(0));
                n.endX = static_cast<float>(nj["endX"].asDouble().unwrapOr(0.0));
                n.latencyTerm = static_cast<float>(nj["latencyTerm"].asDouble().unwrapOr(0.0));
                n.threatCleared = nj.contains("threatCleared")
                                      ? nj["threatCleared"].asBool().unwrapOr(false)
                                      : false;
                n.score = static_cast<float>(nj["score"].asDouble().unwrapOr(0.0));
            }
            e.candNotes.push_back(std::move(n));
        }
    }
    e.candNotesTruncated =
        j.contains("candNotesTruncated") ? j["candNotesTruncated"].asBool().unwrapOr(false) : false;
    if (j.contains("attribution") && j["attribution"].isArray()) {
        for (auto const& aj : j["attribution"].asArray().unwrap()) {
            EbDevRejectLedger::AttributionNote a;
            auto code = std::string(aj["code"].asString().unwrapOr(""));
            if (!EbDevRejectLedger::parseAttribution(code, a.code)) {
                err = "bad-attribution";
                return false;
            }
            if (aj.contains("detail")) {
                a.detail = std::string(aj["detail"].asString().unwrapOr(""));
            }
            e.attribution.push_back(std::move(a));
        }
    }
    if (j.contains("planUs") || j.contains("candidatesTried") || j.contains("adaptiveShell") ||
        j.contains("adaptiveFellThrough") ||
        (j.contains("budgetHit") && e.kind == DevEventKind::PlanFinished)) {
        e.hasPlanCost = true;
        e.planUs = j.contains("planUs") ? static_cast<int>(j["planUs"].asInt().unwrapOr(0)) : 0;
        e.budgetHit = j.contains("budgetHit") ? j["budgetHit"].asBool().unwrapOr(false) : false;
        e.candidatesTried = j.contains("candidatesTried")
                                ? static_cast<int>(j["candidatesTried"].asInt().unwrapOr(0))
                                : 0;
        e.adaptiveShell =
            j.contains("adaptiveShell") ? j["adaptiveShell"].asBool().unwrapOr(false) : false;
        e.adaptiveFellThrough = j.contains("adaptiveFellThrough")
                                    ? j["adaptiveFellThrough"].asBool().unwrapOr(false)
                                    : false;
    }
    return true;
}
