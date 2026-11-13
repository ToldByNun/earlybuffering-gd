#include "../../app/class_manager.hpp"
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

EbDevTraceStore& EbDevTraceStore::get() {
    return EbClassManager::get().service<EbDevTraceStore>();
}

char const* EbDevTraceStore::kindId(DevEventKind kind) {
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
    case DevEventKind::ModeTransition:
        return "modeTransition";
    }
    return "pressObserved";
}

bool EbDevTraceStore::parseKind(std::string const& s, DevEventKind& out) {
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
    if (s == "modeTransition") {
        out = DevEventKind::ModeTransition;
        return true;
    }
    return false;
}

char const* EbDevTraceStore::sourceId(DevEventSource source) {
    switch (source) {
    case DevEventSource::Bake:
        return "bake";
    case DevEventSource::Live:
        break;
    }
    return "live";
}

bool EbDevTraceStore::parseSource(std::string const& s, DevEventSource& out) {
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

bool EbDevTraceStore::validatePressChain(std::vector<EbDevDecisionEvent> const& events, int64_t pressId,
                                       std::string& err) {
    return EbDevTraceValidation::validatePressChain(events, pressId, err);
}

matjson::Value EbDevTraceStoreImplementation::clockStampToJson(EbDevClockContract::Stamp const& st) {
    return EbDevSnapshotCodec::clockStampToJson(st);
}

bool EbDevTraceStoreImplementation::clockStampFromJson(matjson::Value const& j,
                                                     EbDevClockContract::Stamp& st,
                                                     std::string& err) {
    return EbDevSnapshotCodec::clockStampFromJson(j, st, err);
}

matjson::Value EbDevTraceStoreImplementation::playerBlobToJson(EbDevPlayerBlob const& b) {
    return EbDevSnapshotCodec::playerBlobToJson(b);
}

void EbDevTraceStoreImplementation::playerBlobFromJson(matjson::Value const& j, EbDevPlayerBlob& b) {
    return EbDevSnapshotCodec::playerBlobFromJson(j, b);
}

matjson::Value EbDevTraceStoreImplementation::witnessToJson(EbDevLiveWitness const& w) {
    return EbDevSnapshotCodec::witnessToJson(w);
}

bool EbDevTraceStoreImplementation::witnessFromJson(matjson::Value const& j, EbDevLiveWitness& w,
                                                  std::string& err) {
    return EbDevSnapshotCodec::witnessFromJson(j, w, err);
}

matjson::Value EbDevTraceStoreImplementation::eventToJson(EbDevDecisionEvent const& e) {
    return EbDevEventCodec::eventToJson(e);
}

bool EbDevTraceStoreImplementation::eventFromJson(matjson::Value const& j, EbDevDecisionEvent& e,
                                                std::string& err) {
    return EbDevEventCodec::eventFromJson(j, e, err);
}

matjson::Value EbDevTraceStoreImplementation::sessionToJsonValue(EbDevSessionFile const& session) {
    return EbDevSessionCodec::sessionToJsonValue(session);
}

std::string EbDevTraceStore::sessionToJsonString(EbDevSessionFile const& session) {
    return EbDevSessionCodec::sessionToJsonString(session);
}

bool EbDevTraceStore::sessionFromJsonString(std::string const& body, EbDevSessionFile& out,
                                          std::string& err) {
    return EbDevSessionCodec::sessionFromJsonString(body, out, err);
}

bool EbDevTraceStore::isOpen() const {
    return this->m_open;
}

bool EbDevTraceStore::hasOpenAttempt() const {
    if (!(!this->m_open || this->m_session.attempts.empty())) {
        return !this->m_session.attempts.back().sealed;
    }
    return false;
}

std::filesystem::path EbDevTraceStore::tracesDir() const {
    auto* mod = geode::prelude::Mod::get();
    if (!(!mod)) {
        return mod->getSaveDir() / "devtraces";
    }
    return std::filesystem::path("devtraces");
}

std::filesystem::path EbDevTraceStore::sessionPath() const {
    auto id =
        this->m_session.sessionId.empty() ? std::string("unknown") : this->m_session.sessionId;
    return this->tracesDir() / ("session-" + id + ".json");
}

bool EbDevTraceStore::beginSession(std::string sessionId, int recordingsTarget) {
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
    this->m_resourceStats.reset();
    this->m_open = true;
    this->sampleResources();
    return this->flush();
}

void EbDevTraceStore::sampleResources() {
    if (!this->m_open)
        return;
    (void)this->m_resourceStats.noteSample();
    this->m_session.resourceStats = this->m_resourceStats.snapshot();
}

void EbDevTraceStore::setLevelMeta(std::string levelKey, std::string levelName, bool startPos,
                                 bool practice, std::string settingsFingerprint) {
    if (!this->m_open)
        return;
    this->m_session.levelKey = std::move(levelKey);
    this->m_session.levelName = std::move(levelName);
    this->m_session.startPos = startPos;
    this->m_session.practice = practice;
    this->m_session.settingsFingerprint = std::move(settingsFingerprint);
}

void EbDevTraceStore::setModVersion(std::string modVersion) {
    if (!this->m_open)
        return;
    this->m_session.modVersion = std::move(modVersion);
}

std::string EbDevTraceStore::makeSettingsFingerprint() {
    return fmt::format(
        "stage={} mode={} pick={} eng={} ticks={}/{} unit={} safe={} orbs={} "
        "simUs={} tickBud={} twBake={} twSkipDyn={} autoTune={} selfBake={}",
        EbStageConfig::stageLabel(EbStageConfig::fromSettings()),
        EbStageConfig::assistModeName(EbStageConfig::assistMode()), EbStageConfig::assistPickName(),
        EbStageConfig::assistEngineName(), EbStageConfig::ticksAll(), EbStageConfig::ticksWave(),
        EbStageConfig::windowUnitFrames() ? "Frames" : "Ticks", EbStageConfig::safeMode() ? 1 : 0,
        EbStageConfig::assistOrbs() ? 1 : 0, EbStageConfig::simBudgetUs(),
        EbStageConfig::tickBudgetPerPress(), EbStageConfig::triggerBake() ? 1 : 0,
        EbStageConfig::triggerBakeSkipDynamic() ? 1 : 0, EbStageConfig::autoTune() ? 1 : 0,
        EbStageConfig::selfBake() ? 1 : 0);
}

bool EbDevTraceStore::beginAttempt(int startTick) {
    if (!this->m_open)
        return false;
    if (this->hasOpenAttempt())
        return false;

    int64_t nextId = 1;
    for (auto const& existing : this->m_session.attempts) {
        if (existing.attemptId >= nextId)
            nextId = existing.attemptId + 1;
    }

    EbDevAttempt a;
    a.attemptId = nextId;
    a.startTick = startTick;
    a.sealed = false;
    a.counted = false;
    this->m_session.attempts.push_back(std::move(a));
    this->m_nextEventId = 1;
    return true;
}

int64_t EbDevTraceStore::allocPressId() {
    if (!(!this->m_open)) {
        return this->m_nextPressId++;
    }
    return 0;
}

int64_t EbDevTraceStore::nextEventId() {
    return this->m_nextEventId++;
}

EbDevDecisionEvent* EbDevTraceStore::appendEvent(EbDevDecisionEvent event) {
    auto* attempt = this->currentAttempt();
    if (!attempt || attempt->sealed)
        return nullptr;
    bool const attemptScoped = event.kind == DevEventKind::Checkpoint ||
                               event.kind == DevEventKind::ModeTransition;
    if (event.pressId <= 0 && !attemptScoped)
        return nullptr;
    if (attemptScoped) {
        // Attempt timeline — not part of a press parent chain.
        event.pressId = 0;
        event.cycleId = 0;
        event.parentEventId = 0;
    }

    if (event.eventId <= 0) {
        event.eventId = this->nextEventId();
    }
    if (event.attemptId <= 0) {
        event.attemptId = attempt->attemptId;
    }
    if (event.cycleId <= 0 && !attemptScoped) {
        event.cycleId = event.pressId;
    }
    // Plan time is the usual Dynamic hitch window — sample RAM/CPU there.
    bool const sampleNow = event.kind == DevEventKind::PlanFinished;
    attempt->events.push_back(std::move(event));
    if (sampleNow)
        this->sampleResources();
    return &attempt->events.back();
}

void EbDevTraceStore::trimRollingWindow() {
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

bool EbDevTraceStore::sealAttempt(std::string endReason, bool countTowardTarget) {
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
    this->sampleResources();
    return this->flush();
}

bool EbDevTraceStore::sealOfflineEval(int64_t attemptId, EbDevAttemptEval eval) {
    if (!this->m_open || attemptId <= 0)
        return false;
    for (auto& attempt : this->m_session.attempts) {
        if (attempt.attemptId != attemptId)
            continue;
        // AP7: stamp G10 class onto matching planFinished (armed Delay) events.
        if (!eval.simNetClass.empty()) {
            for (auto& ev : attempt.events) {
                if (ev.kind == DevEventKind::PlanFinished && ev.plannedDelay > 0)
                    ev.simNetClass = eval.simNetClass;
            }
        }
        attempt.eval = std::move(eval);
        this->sampleResources();
        return this->flush();
    }
    return false;
}

bool EbDevTraceStore::writeAtomic(matjson::Value const& root) const {
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

bool EbDevTraceStore::flush() {
    if (!(!this->m_open)) {
        return this->writeAtomic(EbDevTraceStoreImplementation::sessionToJsonValue(this->m_session));
    }
    return false;
}

void EbDevTraceStore::closeSession() {
    if (this->m_open && this->hasOpenAttempt()) {
        // Partial attempt kept with endReason quit-unsealed marker, then seal+flush.
        (void)this->sealAttempt("quit", false);
    } else if (this->m_open) {
        (void)this->flush();
    }
    this->m_open = false;
}

EbDevAttempt* EbDevTraceStore::currentAttempt() {
    if (!this->m_open || this->m_session.attempts.empty())
        return nullptr;
    auto& a = this->m_session.attempts.back();
    if (!(a.sealed)) {
        return &a;
    }
    return nullptr;
}

EbDevAttempt const* EbDevTraceStore::currentAttempt() const {
    if (!this->m_open || this->m_session.attempts.empty())
        return nullptr;
    auto const& a = this->m_session.attempts.back();
    if (!(a.sealed)) {
        return &a;
    }
    return nullptr;
}

EbDevDecisionEvent EbDevTraceStoreImplementation::makeEv(DevEventKind kind, int64_t eventId,
                                                     int64_t attemptId, int64_t pressId,
                                                     int64_t parent, int64_t planId) {
    return EbDevTraceValidation::makeEv(kind, eventId, attemptId, pressId, parent, planId);
}

bool EbDevTraceStore::selfCheck(std::string& report) {
    return EbDevTraceValidation::selfCheck(report);
}
