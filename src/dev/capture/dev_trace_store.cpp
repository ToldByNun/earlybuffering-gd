#include "dev_trace_store.hpp"
#include "../../config/stage_config.hpp"
#include "codec/dev_event_codec.hpp"
#include "codec/dev_session_codec.hpp"
#include "codec/dev_snapshot_codec.hpp"
#include "dev_clock_contract.hpp"
#include "dev_live_witness.hpp"
#include "validation/dev_trace_validation.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <cstdio>
#include <fmt/format.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

DevTraceStore& DevTraceStore::get() {
    static DevTraceStore inst;
    return inst;
}

char const* DevTraceStore::kindId(DevEventKind kind) {
    switch (kind) {
    case DevEventKind::PressObserved:
        return "pressObserved";
    case DevEventKind::GatePass:
        return "gatePass";
    case DevEventKind::PlanFinished:
        return "planFinished";
    case DevEventKind::DelayArm:
        return "delayArm";
    case DevEventKind::PassThrough:
        return "passThrough";
    case DevEventKind::Recheck:
        return "recheck";
    case DevEventKind::Hus:
        return "hus";
    case DevEventKind::Fire:
        return "fire";
    case DevEventKind::Checkpoint:
        return "checkpoint";
    case DevEventKind::Outcome:
        return "outcome";
    }
    return "pressObserved";
}

bool DevTraceStore::parseKind(std::string const& s, DevEventKind& out) {
    if (s == "pressObserved") {
        out = DevEventKind::PressObserved;
        return true;
    }
    if (s == "gatePass") {
        out = DevEventKind::GatePass;
        return true;
    }
    if (s == "planFinished") {
        out = DevEventKind::PlanFinished;
        return true;
    }
    if (s == "delayArm") {
        out = DevEventKind::DelayArm;
        return true;
    }
    if (s == "passThrough") {
        out = DevEventKind::PassThrough;
        return true;
    }
    if (s == "recheck") {
        out = DevEventKind::Recheck;
        return true;
    }
    if (s == "hus") {
        out = DevEventKind::Hus;
        return true;
    }
    if (s == "fire") {
        out = DevEventKind::Fire;
        return true;
    }
    if (s == "checkpoint") {
        out = DevEventKind::Checkpoint;
        return true;
    }
    if (s == "outcome") {
        out = DevEventKind::Outcome;
        return true;
    }
    return false;
}

char const* DevTraceStore::sourceId(DevEventSource source) {
    switch (source) {
    case DevEventSource::Bake:
        return "bake";
    case DevEventSource::Live:
        break;
    }
    return "live";
}

bool DevTraceStore::parseSource(std::string const& s, DevEventSource& out) {
    if (s == "bake") {
        out = DevEventSource::Bake;
        return true;
    }
    if (s == "live") {
        out = DevEventSource::Live;
        return true;
    }
    return false;
}

bool DevTraceStore::validatePressChain(std::vector<DevDecisionEvent> const& events, int64_t pressId,
                                       std::string& err) {
    return DevTraceValidation::validatePressChain(events, pressId, err);
}

matjson::Value DevTraceStoreImplementation::clockStampToJson(DevClockContract::Stamp const& st) {
    return DevSnapshotCodec::clockStampToJson(st);
}

bool DevTraceStoreImplementation::clockStampFromJson(matjson::Value const& j,
                                                     DevClockContract::Stamp& st,
                                                     std::string& err) {
    return DevSnapshotCodec::clockStampFromJson(j, st, err);
}

matjson::Value DevTraceStoreImplementation::playerBlobToJson(DevPlayerBlob const& b) {
    return DevSnapshotCodec::playerBlobToJson(b);
}

void DevTraceStoreImplementation::playerBlobFromJson(matjson::Value const& j, DevPlayerBlob& b) {
    return DevSnapshotCodec::playerBlobFromJson(j, b);
}

matjson::Value DevTraceStoreImplementation::witnessToJson(DevLiveWitness const& w) {
    return DevSnapshotCodec::witnessToJson(w);
}

bool DevTraceStoreImplementation::witnessFromJson(matjson::Value const& j, DevLiveWitness& w,
                                                  std::string& err) {
    return DevSnapshotCodec::witnessFromJson(j, w, err);
}

matjson::Value DevTraceStoreImplementation::eventToJson(DevDecisionEvent const& e) {
    return DevEventCodec::eventToJson(e);
}

bool DevTraceStoreImplementation::eventFromJson(matjson::Value const& j, DevDecisionEvent& e,
                                                std::string& err) {
    return DevEventCodec::eventFromJson(j, e, err);
}

matjson::Value DevTraceStoreImplementation::sessionToJsonValue(DevSessionFile const& session) {
    return DevSessionCodec::sessionToJsonValue(session);
}

std::string DevTraceStore::sessionToJsonString(DevSessionFile const& session) {
    return DevSessionCodec::sessionToJsonString(session);
}

bool DevTraceStore::sessionFromJsonString(std::string const& body, DevSessionFile& out,
                                          std::string& err) {
    return DevSessionCodec::sessionFromJsonString(body, out, err);
}

bool DevTraceStore::isOpen() const {
    return this->m_open;
}

bool DevTraceStore::hasOpenAttempt() const {
    if (!(!this->m_open || this->m_session.attempts.empty())) {
        return !this->m_session.attempts.back().sealed;
    }
    return false;
}

std::filesystem::path DevTraceStore::tracesDir() const {
    auto* mod = geode::prelude::Mod::get();
    if (!(!mod)) {
        return mod->getSaveDir() / "devtraces";
    }
    return std::filesystem::path("devtraces");
}

std::filesystem::path DevTraceStore::sessionPath() const {
    auto id =
        this->m_session.sessionId.empty() ? std::string("unknown") : this->m_session.sessionId;
    return this->tracesDir() / ("session-" + id + ".json");
}

bool DevTraceStore::beginSession(std::string sessionId, int recordingsTarget) {
    this->closeSession();
    if (sessionId.empty()) {
        auto const elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();
        sessionId = fmt::format("{}", elapsedMilliseconds);
    }
    if (recordingsTarget < 1)
        recordingsTarget = 1;
    if (recordingsTarget > 50)
        recordingsTarget = 50;

    this->m_session = {};
    this->m_session.traceSchema = kTraceSchema;
    this->m_session.sessionId = std::move(sessionId);
    this->m_session.recordingsTarget = recordingsTarget;
    this->m_session.recordingsDone = 0;
    if (auto* mod = geode::prelude::Mod::get()) {
        this->m_session.modVersion = mod->getVersion().toVString();
    }
    this->m_nextPressId = 1;
    this->m_nextEventId = 1;
    this->m_open = true;
    return this->flush();
}

void DevTraceStore::setLevelMeta(std::string levelKey, std::string levelName, bool startPos,
                                 bool practice, std::string settingsFingerprint) {
    if (!this->m_open)
        return;
    this->m_session.levelKey = std::move(levelKey);
    this->m_session.levelName = std::move(levelName);
    this->m_session.startPos = startPos;
    this->m_session.practice = practice;
    this->m_session.settingsFingerprint = std::move(settingsFingerprint);
}

void DevTraceStore::setModVersion(std::string modVersion) {
    if (!this->m_open)
        return;
    this->m_session.modVersion = std::move(modVersion);
}

std::string DevTraceStore::makeSettingsFingerprint() {
    return fmt::format(
        "stage={} mode={} pick={} eng={} ticks={}/{} unit={} safe={} orbs={} "
        "simUs={} tickBud={} twBake={} twSkipDyn={} autoTune={} selfBake={}",
        StageConfig::stageLabel(StageConfig::fromSettings()),
        StageConfig::assistModeName(StageConfig::assistMode()), StageConfig::assistPickName(),
        StageConfig::assistEngineName(), StageConfig::ticksAll(), StageConfig::ticksWave(),
        StageConfig::windowUnitFrames() ? "Frames" : "Ticks", StageConfig::safeMode() ? 1 : 0,
        StageConfig::assistOrbs() ? 1 : 0, StageConfig::simBudgetUs(),
        StageConfig::tickBudgetPerPress(), StageConfig::triggerBake() ? 1 : 0,
        StageConfig::triggerBakeSkipDynamic() ? 1 : 0, StageConfig::autoTune() ? 1 : 0,
        StageConfig::selfBake() ? 1 : 0);
}

bool DevTraceStore::beginAttempt(int startTick) {
    if (!this->m_open)
        return false;
    if (this->hasOpenAttempt())
        return false;

    int64_t nextId = 1;
    for (auto const& existing : this->m_session.attempts) {
        if (existing.attemptId >= nextId)
            nextId = existing.attemptId + 1;
    }

    DevAttempt a;
    a.attemptId = nextId;
    a.startTick = startTick;
    a.sealed = false;
    a.counted = false;
    this->m_session.attempts.push_back(std::move(a));
    this->m_nextEventId = 1;
    return true;
}

int64_t DevTraceStore::allocPressId() {
    if (!(!this->m_open)) {
        return this->m_nextPressId++;
    }
    return 0;
}

int64_t DevTraceStore::nextEventId() {
    return this->m_nextEventId++;
}

DevDecisionEvent* DevTraceStore::appendEvent(DevDecisionEvent event) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed)
        return nullptr;
    if (event.pressId <= 0)
        return nullptr;

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

void DevTraceStore::trimRollingWindow() {
    int const target = this->m_session.recordingsTarget;
    if (target < 1)
        return;

    // Indices of sealed+counted attempts (ascending = oldest first).
    std::vector<size_t> countedIdx;
    for (size_t index = 0; index < this->m_session.attempts.size(); ++index) {
        auto const& a = this->m_session.attempts[index];
        if (a.sealed && a.counted)
            countedIdx.push_back(index);
    }
    if (static_cast<int>(countedIdx.size()) <= target) {
        this->m_session.recordingsDone = static_cast<int>(countedIdx.size());
        return;
    }

    // Keep from the Nth-last counted attempt onward (drop older attempts).
    size_t const keepFrom = countedIdx[countedIdx.size() - static_cast<size_t>(target)];
    if (keepFrom == 0) {
        this->m_session.recordingsDone = target;
        return;
    }
    this->m_session.attempts.erase(this->m_session.attempts.begin(),
                                   this->m_session.attempts.begin() +
                                       static_cast<std::ptrdiff_t>(keepFrom));
    int kept = 0;
    for (auto const& a : this->m_session.attempts) {
        if (a.sealed && a.counted)
            ++kept;
    }
    this->m_session.recordingsDone = kept;
}

bool DevTraceStore::sealAttempt(std::string endReason, bool countTowardTarget) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed)
        return false;
    attempt->endReason = std::move(endReason);
    attempt->sealed = true;
    attempt->counted = countTowardTarget;
    if (countTowardTarget) {
        ++this->m_session.recordingsDone;
        this->trimRollingWindow();
    }
    return this->flush();
}

bool DevTraceStore::writeAtomic(matjson::Value const& root) const {
    auto const directory = this->tracesDir();
    (void)geode::utils::file::createDirectoryAll(directory);
    auto const path = this->sessionPath();
    auto tmpPath = path;
    tmpPath += ".tmp";

    auto width = geode::utils::file::writeToJson(tmpPath, root);
    if (width.isErr()) {
        // Fallback: direct write if temp failed (e.g. path quirks).
        auto delay = geode::utils::file::writeToJson(path, root);
        return delay.isOk();
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec)
        return true;

    // Windows: replace existing.
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec)
        return true;

    // Last resort: copy then remove tmp.
    std::filesystem::copy_file(tmpPath, path, std::filesystem::copy_options::overwrite_existing,
                               ec);
    std::filesystem::remove(tmpPath, ec);
    return !ec || std::filesystem::exists(path);
}

bool DevTraceStore::flush() {
    if (!(!this->m_open)) {
        return this->writeAtomic(DevTraceStoreImplementation::sessionToJsonValue(this->m_session));
    }
    return false;
}

void DevTraceStore::closeSession() {
    if (this->m_open && this->hasOpenAttempt()) {
        // Partial attempt kept with endReason quit-unsealed marker, then seal+flush.
        (void)this->sealAttempt("quit", false);
    } else if (this->m_open) {
        (void)this->flush();
    }
    this->m_open = false;
}

DevAttempt* DevTraceStore::currentAttempt() {
    if (!this->m_open || this->m_session.attempts.empty())
        return nullptr;
    auto& a = this->m_session.attempts.back();
    if (!(a.sealed)) {
        return &a;
    }
    return nullptr;
}

DevAttempt const* DevTraceStore::currentAttempt() const {
    if (!this->m_open || this->m_session.attempts.empty())
        return nullptr;
    auto const& a = this->m_session.attempts.back();
    if (!(a.sealed)) {
        return &a;
    }
    return nullptr;
}

DevDecisionEvent DevTraceStoreImplementation::makeEv(DevEventKind kind, int64_t eventId,
                                                     int64_t attemptId, int64_t pressId,
                                                     int64_t parent, int64_t planId) {
    return DevTraceValidation::makeEv(kind, eventId, attemptId, pressId, parent, planId);
}

bool DevTraceStore::selfCheck(std::string& report) {
    return DevTraceValidation::selfCheck(report);
}
