#include "dev_session_codec.hpp"
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

matjson::Value EbDevSessionCodec::sessionToJsonValue(EbDevSessionFile const& session) {
    matjson::Value root = matjson::Value::object();
    root["traceSchema"] = session.traceSchema;
    root["modVersion"] = session.modVersion;
    root["sessionId"] = session.sessionId;
    root["levelKey"] = session.levelKey;
    root["levelName"] = session.levelName;
    root["startPos"] = session.startPos;
    root["practice"] = session.practice;
    root["settingsFingerprint"] = session.settingsFingerprint;
    root["recordingsTarget"] = session.recordingsTarget;
    root["recordingsDone"] = session.recordingsDone;
    if (session.resourceStats.valid && session.resourceStats.sampleCount > 0) {
        matjson::Value rs = matjson::Value::object();
        rs["sampleCount"] = session.resourceStats.sampleCount;
        rs["ramMbMin"] = session.resourceStats.ramMbMin;
        rs["ramMbMax"] = session.resourceStats.ramMbMax;
        rs["ramMbLast"] = session.resourceStats.ramMbLast;
        if (session.resourceStats.hasCpu) {
            rs["cpuPctMin"] = session.resourceStats.cpuPctMin;
            rs["cpuPctMax"] = session.resourceStats.cpuPctMax;
            rs["cpuPctLast"] = session.resourceStats.cpuPctLast;
        }
        root["resourceStats"] = rs;
    }

    std::vector<matjson::Value> attempts;
    attempts.reserve(session.attempts.size());
    for (auto const& a : session.attempts) {
        matjson::Value aj = matjson::Value::object();
        aj["attemptId"] = a.attemptId;
        aj["startTick"] = a.startTick;
        aj["endReason"] = a.endReason;
        aj["sealed"] = a.sealed;
        aj["counted"] = a.counted;
        std::vector<matjson::Value> evs;
        evs.reserve(a.events.size());
        for (auto const& e : a.events) {
            evs.push_back(EbDevTraceStoreImplementation::eventToJson(e));
        }
        aj["events"] = evs;
        attempts.push_back(std::move(aj));
    }
    root["attempts"] = attempts;
    return root;
}

std::string EbDevSessionCodec::sessionToJsonString(EbDevSessionFile const& session) {
    return EbDevTraceStoreImplementation::sessionToJsonValue(session).dump();
}

bool EbDevSessionCodec::sessionFromJsonString(std::string const& body, EbDevSessionFile& out,
                                            std::string& err) {
    err.clear();
    out = {};
    auto parsed = matjson::parse(body);
    if (parsed.isErr()) {
        err = "json-parse";
        return false;
    }
    auto root = parsed.unwrap();
    out.traceSchema = root.contains("traceSchema")
                          ? static_cast<int>(root["traceSchema"].asInt().unwrapOr(0))
                          : 0;
    if (out.traceSchema != EbDevTraceStore::kTraceSchema) {
        err = "traceSchema";
        return false;
    }
    out.modVersion =
        root.contains("modVersion") ? std::string(root["modVersion"].asString().unwrapOr("")) : "";
    out.sessionId =
        root.contains("sessionId") ? std::string(root["sessionId"].asString().unwrapOr("")) : "";
    out.levelKey =
        root.contains("levelKey") ? std::string(root["levelKey"].asString().unwrapOr("")) : "";
    out.levelName =
        root.contains("levelName") ? std::string(root["levelName"].asString().unwrapOr("")) : "";
    out.startPos = root.contains("startPos") ? root["startPos"].asBool().unwrapOr(false) : false;
    out.practice = root.contains("practice") ? root["practice"].asBool().unwrapOr(false) : false;
    out.settingsFingerprint = root.contains("settingsFingerprint")
                                  ? std::string(root["settingsFingerprint"].asString().unwrapOr(""))
                                  : "";
    out.recordingsTarget = root.contains("recordingsTarget")
                               ? static_cast<int>(root["recordingsTarget"].asInt().unwrapOr(10))
                               : 10;
    out.recordingsDone = root.contains("recordingsDone")
                             ? static_cast<int>(root["recordingsDone"].asInt().unwrapOr(0))
                             : 0;
    out.resourceStats = {};
    if (root.contains("resourceStats") && root["resourceStats"].isObject()) {
        auto const& rs = root["resourceStats"];
        out.resourceStats.sampleCount =
            rs.contains("sampleCount") ? static_cast<int>(rs["sampleCount"].asInt().unwrapOr(0))
                                       : 0;
        out.resourceStats.ramMbMin =
            rs.contains("ramMbMin") ? rs["ramMbMin"].asDouble().unwrapOr(0.0) : 0.0;
        out.resourceStats.ramMbMax =
            rs.contains("ramMbMax") ? rs["ramMbMax"].asDouble().unwrapOr(0.0) : 0.0;
        out.resourceStats.ramMbLast =
            rs.contains("ramMbLast") ? rs["ramMbLast"].asDouble().unwrapOr(0.0) : 0.0;
        if (rs.contains("cpuPctMin") || rs.contains("cpuPctMax") || rs.contains("cpuPctLast")) {
            out.resourceStats.hasCpu = true;
            out.resourceStats.cpuPctMin =
                rs.contains("cpuPctMin") ? rs["cpuPctMin"].asDouble().unwrapOr(0.0) : 0.0;
            out.resourceStats.cpuPctMax =
                rs.contains("cpuPctMax") ? rs["cpuPctMax"].asDouble().unwrapOr(0.0) : 0.0;
            out.resourceStats.cpuPctLast =
                rs.contains("cpuPctLast") ? rs["cpuPctLast"].asDouble().unwrapOr(0.0) : 0.0;
        }
        out.resourceStats.valid = out.resourceStats.sampleCount > 0;
    }

    if (!root.contains("attempts") || !root["attempts"].isArray()) {
        return true;
    }
    for (auto const& aj : root["attempts"]) {
        EbDevAttempt a;
        a.attemptId = aj.contains("attemptId") ? aj["attemptId"].asInt().unwrapOr(0) : 0;
        a.startTick =
            aj.contains("startTick") ? static_cast<int>(aj["startTick"].asInt().unwrapOr(0)) : 0;
        a.endReason =
            aj.contains("endReason") ? std::string(aj["endReason"].asString().unwrapOr("")) : "";
        a.sealed = aj.contains("sealed") ? aj["sealed"].asBool().unwrapOr(false) : false;
        a.counted = aj.contains("counted") ? aj["counted"].asBool().unwrapOr(false) : false;
        if (aj.contains("events") && aj["events"].isArray()) {
            for (auto const& ej : aj["events"]) {
                EbDevDecisionEvent e;
                std::string eerr;
                if (!EbDevTraceStoreImplementation::eventFromJson(ej, e, eerr)) {
                    err = eerr.empty() ? "bad-event" : eerr;
                    return false;
                }
                a.events.push_back(std::move(e));
            }
        }
        out.attempts.push_back(std::move(a));
    }
    return true;
}
