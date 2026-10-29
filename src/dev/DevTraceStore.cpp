#include "DevTraceStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <unordered_map>
#include <utility>

using namespace geode::prelude;

DevTraceStore& DevTraceStore::get() {
    static DevTraceStore inst;
    return inst;
}

char const* DevTraceStore::kindId(DevEventKind kind) {
    switch (kind) {
        case DevEventKind::PressObserved: return "pressObserved";
        case DevEventKind::GatePass: return "gatePass";
        case DevEventKind::PlanFinished: return "planFinished";
        case DevEventKind::DelayArm: return "delayArm";
        case DevEventKind::PassThrough: return "passThrough";
        case DevEventKind::Recheck: return "recheck";
        case DevEventKind::Hus: return "hus";
        case DevEventKind::Fire: return "fire";
        case DevEventKind::Checkpoint: return "checkpoint";
        case DevEventKind::Outcome: return "outcome";
    }
    return "pressObserved";
}

bool DevTraceStore::parseKind(std::string const& s, DevEventKind& out) {
    if (s == "pressObserved") { out = DevEventKind::PressObserved; return true; }
    if (s == "gatePass") { out = DevEventKind::GatePass; return true; }
    if (s == "planFinished") { out = DevEventKind::PlanFinished; return true; }
    if (s == "delayArm") { out = DevEventKind::DelayArm; return true; }
    if (s == "passThrough") { out = DevEventKind::PassThrough; return true; }
    if (s == "recheck") { out = DevEventKind::Recheck; return true; }
    if (s == "hus") { out = DevEventKind::Hus; return true; }
    if (s == "fire") { out = DevEventKind::Fire; return true; }
    if (s == "checkpoint") { out = DevEventKind::Checkpoint; return true; }
    if (s == "outcome") { out = DevEventKind::Outcome; return true; }
    return false;
}

char const* DevTraceStore::sourceId(DevEventSource source) {
    switch (source) {
        case DevEventSource::Bake: return "bake";
        case DevEventSource::Live: break;
    }
    return "live";
}

bool DevTraceStore::parseSource(std::string const& s, DevEventSource& out) {
    if (s == "bake") { out = DevEventSource::Bake; return true; }
    if (s == "live") { out = DevEventSource::Live; return true; }
    return false;
}

bool DevTraceStore::validatePressChain(
    std::vector<DevDecisionEvent> const& events,
    int64_t pressId,
    std::string& err
) {
    err.clear();
    if (pressId <= 0) {
        err = "pressId<=0";
        return false;
    }

    std::vector<DevDecisionEvent const*> chain;
    chain.reserve(events.size());
    for (auto const& e : events) {
        if (e.pressId == pressId) {
            chain.push_back(&e);
        }
    }
    if (chain.empty()) {
        err = "no-events-for-pressId";
        return false;
    }

    // eventId must be strictly increasing along the chain order we validate.
    for (size_t i = 1; i < chain.size(); ++i) {
        if (chain[i]->eventId <= chain[i - 1]->eventId) {
            err = "eventId-not-monotonic";
            return false;
        }
    }

    std::unordered_map<int64_t, DevDecisionEvent const*> byId;
    byId.reserve(chain.size());

    DevDecisionEvent const* press = nullptr;
    for (auto const* e : chain) {
        if (e->eventId <= 0) {
            err = "eventId<=0";
            return false;
        }
        if (e->cycleId != e->pressId) {
            err = "cycleId!=pressId";
            return false;
        }
        if (byId.count(e->eventId)) {
            err = "duplicate-eventId";
            return false;
        }
        byId[e->eventId] = e;

        if (e->kind == DevEventKind::PressObserved) {
            if (press) {
                err = "duplicate-pressObserved";
                return false;
            }
            if (e->parentEventId != 0) {
                err = "pressObserved-parent-not-0";
                return false;
            }
            press = e;
        }
        else {
            if (e->parentEventId <= 0) {
                err = "missing-parentEventId";
                return false;
            }
            auto it = byId.find(e->parentEventId);
            if (it == byId.end()) {
                // Parent must already appear earlier in the same press chain.
                err = "parent-not-in-chain";
                return false;
            }
            if (it->second->pressId != pressId) {
                err = "parent-pressId-mismatch";
                return false;
            }
        }
    }

    if (!press) {
        err = "missing-pressObserved";
        return false;
    }

    // Required terminal shape for a Delay arm cycle: … → fire → outcome
    // Gate-only cycles: press → gatePass → outcome (parent = gatePass)
    bool hasFire = false;
    bool hasOutcome = false;
    bool hasGate = false;
    bool hasPlan = false;
    bool hasArm = false;
    bool hasPassThrough = false;
    for (auto const* e : chain) {
        switch (e->kind) {
            case DevEventKind::GatePass: hasGate = true; break;
            case DevEventKind::PlanFinished: hasPlan = true; break;
            case DevEventKind::DelayArm: hasArm = true; break;
            case DevEventKind::PassThrough: hasPassThrough = true; break;
            case DevEventKind::Fire: hasFire = true; break;
            case DevEventKind::Outcome: hasOutcome = true; break;
            default: break;
        }
        if (e->kind != DevEventKind::PressObserved && e->attemptId != press->attemptId) {
            err = "attemptId-mismatch";
            return false;
        }
    }

    if (!hasOutcome) {
        err = "missing-outcome";
        return false;
    }

    if (hasArm) {
        if (!hasPlan) {
            err = "delayArm-without-planFinished";
            return false;
        }
        if (!hasFire) {
            err = "delayArm-without-fire";
            return false;
        }
    }

    if (hasGate && hasPlan) {
        err = "gatePass-and-planFinished";
        return false;
    }

    if (hasPassThrough && hasArm) {
        err = "passThrough-and-delayArm";
        return false;
    }

    // Outcome parent must be fire, passThrough, or gatePass.
    for (auto const* e : chain) {
        if (e->kind != DevEventKind::Outcome) continue;
        auto it = byId.find(e->parentEventId);
        if (it == byId.end()) {
            err = "outcome-parent-missing";
            return false;
        }
        auto const pk = it->second->kind;
        if (pk != DevEventKind::Fire
            && pk != DevEventKind::PassThrough
            && pk != DevEventKind::GatePass) {
            err = "outcome-parent-kind";
            return false;
        }
    }

    // Fire parent must be delayArm, recheck, or hus.
    for (auto const* e : chain) {
        if (e->kind != DevEventKind::Fire) continue;
        auto it = byId.find(e->parentEventId);
        if (it == byId.end()) {
            err = "fire-parent-missing";
            return false;
        }
        auto const pk = it->second->kind;
        if (pk != DevEventKind::DelayArm
            && pk != DevEventKind::Recheck
            && pk != DevEventKind::Hus) {
            err = "fire-parent-kind";
            return false;
        }
    }

    return true;
}

static matjson::Value eventToJson(DevDecisionEvent const& e) {
    matjson::Value j = matjson::Value::object();
    j["eventId"] = e.eventId;
    j["attemptId"] = e.attemptId;
    j["pressId"] = e.pressId;
    j["cycleId"] = e.cycleId;
    j["planId"] = e.planId;
    j["parentEventId"] = e.parentEventId;
    j["playerIndex"] = e.playerIndex;
    j["down"] = e.down;
    j["source"] = DevTraceStore::sourceId(e.source);
    j["kind"] = DevTraceStore::kindId(e.kind);
    j["effectTick"] = e.effectTick;
    j["pressTick"] = e.pressTick;
    j["plannedDelay"] = e.plannedDelay;
    j["plannedFireTick"] = e.plannedFireTick;
    j["actualFireTick"] = e.actualFireTick;
    if (!e.assistReason.empty()) j["assistReason"] = e.assistReason;
    if (!e.endReason.empty()) j["endReason"] = e.endReason;
    return j;
}

static bool eventFromJson(matjson::Value const& j, DevDecisionEvent& e, std::string& err) {
    e = {};
    e.eventId = j.contains("eventId") ? j["eventId"].asInt().unwrapOr(0) : 0;
    e.attemptId = j.contains("attemptId") ? j["attemptId"].asInt().unwrapOr(0) : 0;
    e.pressId = j.contains("pressId") ? j["pressId"].asInt().unwrapOr(0) : 0;
    e.cycleId = j.contains("cycleId") ? j["cycleId"].asInt().unwrapOr(0) : 0;
    e.planId = j.contains("planId") ? j["planId"].asInt().unwrapOr(0) : 0;
    e.parentEventId = j.contains("parentEventId") ? j["parentEventId"].asInt().unwrapOr(0) : 0;
    e.playerIndex = j.contains("playerIndex")
        ? static_cast<int>(j["playerIndex"].asInt().unwrapOr(0))
        : 0;
    e.down = j.contains("down") ? j["down"].asBool().unwrapOr(true) : true;
    auto src = j.contains("source") ? std::string(j["source"].asString().unwrapOr("live")) : "live";
    if (!DevTraceStore::parseSource(src, e.source)) {
        err = "bad-source";
        return false;
    }
    auto kind = j.contains("kind") ? std::string(j["kind"].asString().unwrapOr("")) : "";
    if (!DevTraceStore::parseKind(kind, e.kind)) {
        err = "bad-kind";
        return false;
    }
    e.effectTick = j.contains("effectTick")
        ? static_cast<int>(j["effectTick"].asInt().unwrapOr(0))
        : 0;
    e.pressTick = j.contains("pressTick")
        ? static_cast<int>(j["pressTick"].asInt().unwrapOr(0))
        : 0;
    e.plannedDelay = j.contains("plannedDelay")
        ? static_cast<int>(j["plannedDelay"].asInt().unwrapOr(0))
        : 0;
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
    return true;
}

static matjson::Value sessionToJsonValue(DevSessionFile const& session) {
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

    std::vector<matjson::Value> attempts;
    attempts.reserve(session.attempts.size());
    for (auto const& a : session.attempts) {
        matjson::Value aj = matjson::Value::object();
        aj["attemptId"] = a.attemptId;
        aj["startTick"] = a.startTick;
        aj["endReason"] = a.endReason;
        aj["sealed"] = a.sealed;
        std::vector<matjson::Value> evs;
        evs.reserve(a.events.size());
        for (auto const& e : a.events) {
            evs.push_back(eventToJson(e));
        }
        aj["events"] = evs;
        attempts.push_back(std::move(aj));
    }
    root["attempts"] = attempts;
    return root;
}

std::string DevTraceStore::sessionToJsonString(DevSessionFile const& session) {
    return sessionToJsonValue(session).dump();
}

bool DevTraceStore::sessionFromJsonString(
    std::string const& body, DevSessionFile& out, std::string& err
) {
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
    if (out.traceSchema != kTraceSchema) {
        err = "traceSchema";
        return false;
    }
    out.modVersion = root.contains("modVersion")
        ? std::string(root["modVersion"].asString().unwrapOr(""))
        : "";
    out.sessionId = root.contains("sessionId")
        ? std::string(root["sessionId"].asString().unwrapOr(""))
        : "";
    out.levelKey = root.contains("levelKey")
        ? std::string(root["levelKey"].asString().unwrapOr(""))
        : "";
    out.levelName = root.contains("levelName")
        ? std::string(root["levelName"].asString().unwrapOr(""))
        : "";
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

    if (!root.contains("attempts") || !root["attempts"].isArray()) {
        return true;
    }
    for (auto const& aj : root["attempts"]) {
        DevAttempt a;
        a.attemptId = aj.contains("attemptId") ? aj["attemptId"].asInt().unwrapOr(0) : 0;
        a.startTick = aj.contains("startTick")
            ? static_cast<int>(aj["startTick"].asInt().unwrapOr(0))
            : 0;
        a.endReason = aj.contains("endReason")
            ? std::string(aj["endReason"].asString().unwrapOr(""))
            : "";
        a.sealed = aj.contains("sealed") ? aj["sealed"].asBool().unwrapOr(false) : false;
        if (aj.contains("events") && aj["events"].isArray()) {
            for (auto const& ej : aj["events"]) {
                DevDecisionEvent e;
                std::string eerr;
                if (!eventFromJson(ej, e, eerr)) {
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

bool DevTraceStore::isOpen() const {
    return this->m_open;
}

bool DevTraceStore::hasOpenAttempt() const {
    if (!this->m_open || this->m_session.attempts.empty()) return false;
    return !this->m_session.attempts.back().sealed;
}

std::filesystem::path DevTraceStore::tracesDir() const {
    auto* mod = Mod::get();
    if (!mod) return std::filesystem::path("devtraces");
    return mod->getSaveDir() / "devtraces";
}

std::filesystem::path DevTraceStore::sessionPath() const {
    auto id = this->m_session.sessionId.empty() ? std::string("unknown") : this->m_session.sessionId;
    return this->tracesDir() / ("session-" + id + ".json");
}

bool DevTraceStore::beginSession(std::string sessionId, int recordingsTarget) {
    this->closeSession();
    if (sessionId.empty()) {
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        sessionId = fmt::format("{}", ms);
    }
    if (recordingsTarget < 1) recordingsTarget = 1;
    if (recordingsTarget > 50) recordingsTarget = 50;

    this->m_session = {};
    this->m_session.traceSchema = kTraceSchema;
    this->m_session.sessionId = std::move(sessionId);
    this->m_session.recordingsTarget = recordingsTarget;
    this->m_session.recordingsDone = 0;
    if (auto* mod = Mod::get()) {
        this->m_session.modVersion = mod->getVersion().toVString();
    }
    this->m_nextPressId = 1;
    this->m_nextEventId = 1;
    this->m_open = true;
    return this->flush();
}

void DevTraceStore::setLevelMeta(
    std::string levelKey,
    std::string levelName,
    bool startPos,
    bool practice,
    std::string settingsFingerprint
) {
    if (!this->m_open) return;
    this->m_session.levelKey = std::move(levelKey);
    this->m_session.levelName = std::move(levelName);
    this->m_session.startPos = startPos;
    this->m_session.practice = practice;
    this->m_session.settingsFingerprint = std::move(settingsFingerprint);
}

void DevTraceStore::setModVersion(std::string modVersion) {
    if (!this->m_open) return;
    this->m_session.modVersion = std::move(modVersion);
}

bool DevTraceStore::beginAttempt(int startTick) {
    if (!this->m_open) return false;
    if (this->hasOpenAttempt()) return false;

    DevAttempt a;
    a.attemptId = static_cast<int64_t>(this->m_session.attempts.size()) + 1;
    a.startTick = startTick;
    a.sealed = false;
    this->m_session.attempts.push_back(std::move(a));
    this->m_nextEventId = 1;
    return true;
}

int64_t DevTraceStore::allocPressId() {
    if (!this->m_open) return 0;
    return this->m_nextPressId++;
}

int64_t DevTraceStore::nextEventId() {
    return this->m_nextEventId++;
}

DevDecisionEvent* DevTraceStore::appendEvent(DevDecisionEvent event) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed) return nullptr;
    if (event.pressId <= 0) return nullptr;

    if (event.eventId <= 0) {
        event.eventId = this->nextEventId();
    }
    if (event.attemptId <= 0) {
        event.attemptId = attempt->attemptId;
    }
    if (event.cycleId <= 0) {
        event.cycleId = event.pressId;
    }
    attempt->events.push_back(std::move(event));
    return &attempt->events.back();
}

bool DevTraceStore::sealAttempt(std::string endReason, bool countTowardTarget) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed) return false;
    attempt->endReason = std::move(endReason);
    attempt->sealed = true;
    if (countTowardTarget) {
        ++this->m_session.recordingsDone;
    }
    return this->flush();
}

bool DevTraceStore::writeAtomic(matjson::Value const& root) const {
    auto const dir = this->tracesDir();
    (void)file::createDirectoryAll(dir);
    auto const path = this->sessionPath();
    auto tmpPath = path;
    tmpPath += ".tmp";

    auto w = file::writeToJson(tmpPath, root);
    if (w.isErr()) {
        // Fallback: direct write if temp failed (e.g. path quirks).
        auto d = file::writeToJson(path, root);
        return d.isOk();
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec) return true;

    // Windows: replace existing.
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec) return true;

    // Last resort: copy then remove tmp.
    std::filesystem::copy_file(
        tmpPath, path,
        std::filesystem::copy_options::overwrite_existing, ec
    );
    std::filesystem::remove(tmpPath, ec);
    return !ec || std::filesystem::exists(path);
}

bool DevTraceStore::flush() {
    if (!this->m_open) return false;
    return this->writeAtomic(sessionToJsonValue(this->m_session));
}

void DevTraceStore::closeSession() {
    if (this->m_open && this->hasOpenAttempt()) {
        // Partial attempt kept with endReason quit-unsealed marker, then seal+flush.
        (void)this->sealAttempt("quit", false);
    }
    else if (this->m_open) {
        (void)this->flush();
    }
    this->m_open = false;
}

DevAttempt* DevTraceStore::currentAttempt() {
    if (!this->m_open || this->m_session.attempts.empty()) return nullptr;
    auto& a = this->m_session.attempts.back();
    if (a.sealed) return nullptr;
    return &a;
}

DevAttempt const* DevTraceStore::currentAttempt() const {
    if (!this->m_open || this->m_session.attempts.empty()) return nullptr;
    auto const& a = this->m_session.attempts.back();
    if (a.sealed) return nullptr;
    return &a;
}

static DevDecisionEvent makeEv(
    DevEventKind kind,
    int64_t eventId,
    int64_t attemptId,
    int64_t pressId,
    int64_t parent,
    int64_t planId
) {
    DevDecisionEvent e;
    e.kind = kind;
    e.eventId = eventId;
    e.attemptId = attemptId;
    e.pressId = pressId;
    e.cycleId = pressId;
    e.parentEventId = parent;
    e.planId = planId;
    e.down = true;
    e.source = DevEventSource::Live;
    return e;
}

bool DevTraceStore::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    expect(std::strcmp(kindId(DevEventKind::PressObserved), "pressObserved") == 0, "kind-press");
    expect(std::strcmp(kindId(DevEventKind::Outcome), "outcome") == 0, "kind-outcome");
    DevEventKind k = DevEventKind::PressObserved;
    expect(parseKind("delayArm", k) && k == DevEventKind::DelayArm, "parse-delayArm");
    expect(!parseKind("nope", k), "parse-kind-fail");

    // Valid Delay chain: press → plan → arm → fire → outcome
    std::vector<DevDecisionEvent> good;
    good.push_back(makeEv(DevEventKind::PressObserved, 1, 1, 10, 0, 0));
    good.push_back(makeEv(DevEventKind::PlanFinished, 2, 1, 10, 1, 42));
    good.push_back(makeEv(DevEventKind::DelayArm, 3, 1, 10, 2, 42));
    good.push_back(makeEv(DevEventKind::Fire, 4, 1, 10, 3, 42));
    good.push_back(makeEv(DevEventKind::Outcome, 5, 1, 10, 4, 42));
    std::string err;
    expect(validatePressChain(good, 10, err), "chain-good");

    // Missing parentEventId on fire
    auto badParent = good;
    badParent[3].parentEventId = 0;
    expect(!validatePressChain(badParent, 10, err), "chain-missing-parent");
    expect(err == "missing-parentEventId", "chain-missing-parent-msg");

    // Parent not in chain
    auto badRef = good;
    badRef[3].parentEventId = 99;
    expect(!validatePressChain(badRef, 10, err), "chain-parent-missing");
    expect(err == "parent-not-in-chain", "chain-parent-missing-msg");

    // Gate cycle: press → gatePass → outcome
    std::vector<DevDecisionEvent> gate;
    gate.push_back(makeEv(DevEventKind::PressObserved, 1, 1, 7, 0, 0));
    gate.push_back(makeEv(DevEventKind::GatePass, 2, 1, 7, 1, 0));
    gate.push_back(makeEv(DevEventKind::Outcome, 3, 1, 7, 2, 0));
    expect(validatePressChain(gate, 7, err), "chain-gate");

    // JSON roundtrip preserves IDs
    DevSessionFile sess;
    sess.traceSchema = kTraceSchema;
    sess.modVersion = "v1.3.0-test";
    sess.sessionId = "selfcheck";
    sess.recordingsTarget = 10;
    sess.recordingsDone = 1;
    DevAttempt att;
    att.attemptId = 1;
    att.startTick = 0;
    att.endReason = "death";
    att.sealed = true;
    att.events = good;
    sess.attempts.push_back(att);

    auto body = sessionToJsonString(sess);
    expect(!body.empty(), "json-nonempty");
    DevSessionFile back;
    std::string jerr;
    expect(sessionFromJsonString(body, back, jerr), "json-parse-ok");
    expect(back.traceSchema == kTraceSchema, "json-schema");
    expect(back.sessionId == "selfcheck", "json-sessionId");
    expect(back.attempts.size() == 1, "json-attempts");
    expect(back.attempts[0].events.size() == 5, "json-events");
    expect(back.attempts[0].events[0].pressId == 10, "json-pressId");
    expect(back.attempts[0].events[1].planId == 42, "json-planId");
    expect(back.attempts[0].events[3].parentEventId == 3, "json-parent");
    expect(validatePressChain(back.attempts[0].events, 10, err), "json-chain");

    // Store API: begin/append/seal/flush (disk) when Mod is available.
    auto& store = DevTraceStore::get();
    bool const wasOpen = store.isOpen();
    DevSessionFile saved = store.session();
    expect(store.beginSession("selfcheck-api", 3), "store-begin");
    store.setModVersion("v1.3.0-test");
    store.setLevelMeta("lvl", "Level", false, true, "fp");
    expect(store.beginAttempt(100), "store-attempt");
    int64_t const pid = store.allocPressId();
    expect(pid > 0, "store-pressId");

    DevDecisionEvent e1 = makeEv(DevEventKind::PressObserved, 0, 0, pid, 0, 0);
    e1.pressTick = 100;
    expect(store.appendEvent(e1) != nullptr, "store-append-press");
    auto* pPlan = store.appendEvent(makeEv(DevEventKind::PlanFinished, 0, 0, pid, 0, 9));
    expect(pPlan != nullptr, "store-append-plan");
    // Fix parent to press eventId after auto-assign.
    auto* cur = store.currentAttempt();
    expect(cur && cur->events.size() >= 2, "store-two-events");
    if (cur && cur->events.size() >= 2) {
        cur->events[1].parentEventId = cur->events[0].eventId;
        cur->events[1].planId = 9;
        auto* arm = store.appendEvent(makeEv(
            DevEventKind::DelayArm, 0, 0, pid, cur->events[1].eventId, 9
        ));
        expect(arm != nullptr, "store-append-arm");
        auto* fire = store.appendEvent(makeEv(
            DevEventKind::Fire, 0, 0, pid, arm->eventId, 9
        ));
        expect(fire != nullptr, "store-append-fire");
        auto* out = store.appendEvent(makeEv(
            DevEventKind::Outcome, 0, 0, pid, fire->eventId, 9
        ));
        expect(out != nullptr, "store-append-outcome");
        expect(validatePressChain(cur->events, pid, err), "store-chain");
    }
    expect(store.sealAttempt("death", true), "store-seal");
    expect(store.session().recordingsDone == 1, "store-count");

    // Reload from disk
    auto path = store.sessionPath();
    expect(std::filesystem::exists(path), "store-file-exists");
    std::ifstream in(path);
    expect(static_cast<bool>(in), "store-file-open");
    std::string diskBody(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );
    DevSessionFile diskSess;
    expect(sessionFromJsonString(diskBody, diskSess, jerr), "store-disk-parse");
    expect(diskSess.attempts.size() == 1, "store-disk-attempt");
    if (!diskSess.attempts.empty()) {
        expect(
            validatePressChain(diskSess.attempts[0].events, pid, err),
            "store-disk-chain"
        );
    }

    // Cleanup selfcheck file; restore previous open state lightly.
    store.closeSession();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (wasOpen) {
        // Do not resurrect prior session contents; callers must reopen.
        (void)saved;
    }

    if (fails == 0) {
        report = "DevTraceStore selfCheck OK";
    }
    else {
        report = fmt::format("DevTraceStore {} fails: {}", fails, report);
    }
    return fails == 0;
}
