#include "real_assist_runner.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_controller.hpp"
#include "../bake/bake_map.hpp"
#include "../diagnostics/assist_perf_stats.hpp"
#include "../diagnostics/assist_real_metrics.hpp"
#include "../learning/hybrid_learning.hpp"
#include "../platform/save_drop_paths.hpp"
#include "../replay/replay_import.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/shadow_simulator.hpp"
#include "gold_reproduce_gate.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <system_error>

bool EbRealAssistRunner::s_autoArmed = false;
bool EbRealAssistRunner::s_autoFired = false;
EbRealAssistRunner::AutoKind EbRealAssistRunner::s_autoKind = EbRealAssistRunner::AutoKind::None;
std::vector<EbRealAssistRunner::CaseSpec> EbRealAssistRunner::s_modesQueue{};
size_t EbRealAssistRunner::s_modesQueueIndex = 0;
bool EbRealAssistRunner::s_modesAwaitMenu = false;
std::vector<std::string> EbRealAssistRunner::s_modesQueueResults{};

EbRealAssistRunner& EbRealAssistRunner::get() {
    static EbRealAssistRunner runner;
    return runner;
}

char const* EbRealAssistRunner::protocolName(Protocol protocol) {
    return protocol == Protocol::Online ? "Online" : "Frozen";
}

bool EbRealAssistRunner::parseProtocol(std::string const& text, Protocol& out) {
    std::string value = text;
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    if (value.empty() || value == "frozen") {
        out = Protocol::Frozen;
        return true;
    }
    if (value == "online") {
        out = Protocol::Online;
        return true;
    }
    return false;
}

bool EbRealAssistRunner::parseMode(std::string const& text, EbStageConfig::AssistMode& out) {
    std::string value = text;
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    if (value.empty() || value == "live") {
        out = EbStageConfig::AssistMode::Live;
        return true;
    }
    if (value == "hybrid") {
        out = EbStageConfig::AssistMode::Hybrid;
        return true;
    }
    if (value == "ai") {
        out = EbStageConfig::AssistMode::AI;
        return true;
    }
    return false;
}

bool EbRealAssistRunner::parseCaseString(std::string const& text, CaseSpec& out, std::string& err) {
    err.clear();
    out = CaseSpec{};
    if (text.empty() || text == "matrix" || text == "all") {
        err = "matrix/all is not a single case";
        return false;
    }
    auto slash = text.find('/');
    if (slash == std::string::npos) {
        out.corpusId = text;
        out.variant = "pulse-shift-early-2";
    } else {
        out.corpusId = text.substr(0, slash);
        out.variant = text.substr(slash + 1);
    }
    if (out.corpusId.empty() || out.variant.empty()) {
        err = "expected id or id/variant";
        return false;
    }
    if (out.corpusId == "slaughterhouse")
        out.wavePattern = true;
    if (out.corpusId == "hoveropolis")
        out.shipLine = true;
    return true;
}

std::vector<EbRealAssistRunner::CaseSpec> EbRealAssistRunner::smokeMatrix() {
    std::vector<CaseSpec> rows;
    char const* ids[] = {"cataclysm", "quantum", "slaughterhouse", "hoveropolis"};
    char const* variants[] = {"on-time", "pulse-shift-early-2"};
    EbStageConfig::AssistMode modes[] = {EbStageConfig::AssistMode::Live,
                                         EbStageConfig::AssistMode::Hybrid};
    for (auto const* id : ids) {
        for (auto mode : modes) {
            for (auto const* variant : variants) {
                CaseSpec spec;
                spec.corpusId = id;
                spec.variant = variant;
                spec.mode = mode;
                spec.protocol = Protocol::Frozen;
                spec.wavePattern = std::string(id) == "slaughterhouse";
                spec.shipLine = std::string(id) == "hoveropolis";
                rows.push_back(spec);
            }
        }
    }
    return rows;
}

std::vector<EbRealAssistRunner::CaseSpec> EbRealAssistRunner::modesCaseQueue() {
    // APR4 smoke keys — one Live/Hybrid compare per corpus (modes planned in-run).
    std::vector<CaseSpec> rows;
    char const* keys[][2] = {{"cataclysm", "pulse-shift-early-2"},
                             {"quantum", "on-time"},
                             {"slaughterhouse", "pulse-shift-early-2"},
                             {"hoveropolis", "pulse-shift-early-2"}};
    for (auto const& key : keys) {
        CaseSpec spec;
        spec.corpusId = key[0];
        spec.variant = key[1];
        spec.mode = EbStageConfig::AssistMode::Hybrid;
        spec.protocol = Protocol::Online;
        spec.wavePattern = spec.corpusId == "slaughterhouse";
        spec.shipLine = spec.corpusId == "hoveropolis";
        rows.push_back(spec);
    }
    return rows;
}

std::string EbRealAssistRunner::envOrEmpty(char const* name) {
    if (!name)
        return {};
#if defined(_MSC_VER)
    char* buffer = nullptr;
    size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || !buffer)
        return {};
    std::string value(buffer);
    free(buffer);
    return value;
#else
    char const* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
#endif
}

bool EbRealAssistRunner::envTruthy(char const* name) {
    auto value = envOrEmpty(name);
    if (value.empty())
        return false;
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return value != "0" && value != "false" && value != "off" && value != "no";
}

bool EbRealAssistRunner::active() const {
    return m_active;
}

EbRealAssistRunner::CaseSpec const& EbRealAssistRunner::current() const {
    return m_case;
}

bool EbRealAssistRunner::resolvePaths(CaseSpec const& spec, std::filesystem::path& levelPath,
                                      std::filesystem::path& replayPath, std::string& err) const {
    err.clear();
    auto const root = EbGoldReproduceGate::corpusRoot();
    levelPath = root / spec.corpusId / "level.gmd";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(levelPath, ec) || ec) {
        err = fmt::format("missing level {}", levelPath.string());
        return false;
    }

    auto const derived = root / "derived" / spec.corpusId;
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(derived / (spec.variant + ".gdr2"));
    if (std::filesystem::is_directory(derived, ec)) {
        for (auto const& entry : std::filesystem::directory_iterator(derived, ec)) {
            if (ec || !entry.is_regular_file())
                continue;
            auto name = entry.path().filename().string();
            if (name.rfind(spec.variant, 0) == 0 && name.size() > 5 && name.size() >= 5 &&
                name.compare(name.size() - 5, 5, ".gdr2") == 0) {
                candidates.push_back(entry.path());
            }
        }
    }
    auto const fixtureDerived =
        std::filesystem::path("tools") / "fixtures" / "real_corpus" / "derived" / spec.corpusId;
    if (std::filesystem::is_directory(fixtureDerived, ec)) {
        for (auto const& entry : std::filesystem::directory_iterator(fixtureDerived, ec)) {
            if (ec || !entry.is_regular_file())
                continue;
            auto name = entry.path().filename().string();
            bool const isGdr2 =
                name.size() >= 5 && name.compare(name.size() - 5, 5, ".gdr2") == 0;
            if ((name == spec.variant + ".gdr2") ||
                (name.rfind(spec.variant + "__", 0) == 0 && isGdr2)) {
                candidates.push_back(entry.path());
            }
        }
    }
    if (spec.variant == "on-time")
        candidates.push_back(root / spec.corpusId / "clear.gdr2");

    for (auto const& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
            replayPath = candidate;
            return true;
        }
    }
    err = fmt::format("missing derived replay for {}/{} under {}", spec.corpusId, spec.variant,
                      derived.string());
    return false;
}

bool EbRealAssistRunner::applyProtocol(CaseSpec const& spec, std::string& err) {
    err.clear();
    auto* mod = geode::prelude::Mod::get();
    if (!mod) {
        err = "Mod::get null";
        return false;
    }
    m_prevAutoTune = EbStageConfig::autoTuneEnabled();
    m_prevHybridFrozen = EbHybridLearning::get().frozen();
    EbStageConfig::setAssistMode(spec.mode);
    try {
        mod->setSettingValue<bool>("enabled", true);
    } catch (...) {
    }

    // Frozen Live: Auto-Tune off (pure Live plan).
    // Frozen Hybrid: Auto-Tune on + model freeze(true) so hybrid-first still READs,
    // but learns nothing (write-guard). Online Hybrid: Auto-Tune on, freeze(false).
    bool const hybridMode = spec.mode == EbStageConfig::AssistMode::Hybrid;
    if (spec.protocol == Protocol::Frozen) {
        try {
            mod->setSettingValue<bool>("auto-tune", hybridMode);
        } catch (...) {
        }
        EbStageConfig::refreshCache();
        EbHybridLearning::get().freeze(true);
        if (hybridMode)
            EbHybridLearning::get().settingsChanged();
    } else {
        if (!hybridMode)
            EbHybridLearning::get().reset();
        try {
            mod->setSettingValue<bool>("auto-tune", true);
        } catch (...) {
        }
        EbStageConfig::refreshCache();
        EbHybridLearning::get().freeze(false);
        if (hybridMode)
            EbHybridLearning::get().settingsChanged();
    }
    m_protocolApplied = true;
    return true;
}

bool EbRealAssistRunner::restoreProtocol(std::string& err) {
    err.clear();
    if (!m_protocolApplied)
        return true;
    auto* mod = geode::prelude::Mod::get();
    if (mod) {
        try {
            mod->setSettingValue<bool>("auto-tune", m_prevAutoTune);
        } catch (...) {
        }
    }
    EbStageConfig::refreshCache();
    EbHybridLearning::get().freeze(m_prevHybridFrozen);
    m_protocolApplied = false;
    return true;
}

bool EbRealAssistRunner::importReplayFile(std::filesystem::path const& path, std::string& err) {
    err.clear();
    auto loaded = EbReplayImport::load(path);
    if (loaded.isErr()) {
        err = loaded.unwrapErr();
        return false;
    }
    EbBakeMap::get().setPendingReplay(loaded.unwrap());
    auto const drop =
        EbSaveDropPaths::get().replayDropDir() /
        fmt::format("real_assist_{}_{}.gdr2", m_case.corpusId, m_case.variant);
    std::error_code ec;
    std::filesystem::create_directories(EbSaveDropPaths::get().replayDropDir(), ec);
    std::filesystem::copy_file(path, drop, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        EbAssistLog::info(fmt::format("WARN real-assist stage copy: {}", ec.message()));
    return true;
}

bool EbRealAssistRunner::writeLastRun(std::string const& text, std::string& err) const {
    err.clear();
    auto const dir = EbSaveDropPaths::get().rootDir() / "real_assist";
    auto created = geode::utils::file::createDirectoryAll(dir);
    if (created.isErr()) {
        err = created.unwrapErr();
        return false;
    }
    auto written = geode::utils::file::writeString(dir / "last_run.txt", text);
    if (written.isErr()) {
        err = written.unwrapErr();
        return false;
    }
    return true;
}

bool EbRealAssistRunner::writeSession(std::string& err) const {
    err.clear();
    matjson::Value root = matjson::Value::object();
    root["schema"] = 1;
    root["corpusId"] = m_case.corpusId;
    root["variant"] = m_case.variant;
    root["mode"] = EbStageConfig::assistModeName(m_case.mode);
    root["protocol"] = protocolName(m_case.protocol);
    root["shipLine"] = m_case.shipLine;
    root["wavePattern"] = m_case.wavePattern;
    root["active"] = m_active;
    root["levelOpened"] = m_levelOpened;
    root["modesLiveBot"] = true;
    root["reproducePass"] = EbGoldReproduceGate::statusIsPass(m_case.corpusId);

    matjson::Value attempts = matjson::Value::array();
    int completes = 0;
    for (auto const& row : m_attempts) {
        matjson::Value item = matjson::Value::object();
        item["index"] = row.index;
        item["completed"] = row.completed;
        item["endPercent"] = row.endPercent;
        item["arms"] = row.arms;
        item["daf"] = row.daf;
        item["nOpp"] = row.nOpp;
        item["budgetHitRate"] = row.budgetHitRate;
        item["avgPlanUs"] = row.avgPlanUs;
        item["assistTouched"] = row.assistTouched;
        attempts.push(item);
        if (row.completed)
            ++completes;
    }
    root["attempts"] = attempts;
    float surviveRate =
        m_attempts.empty() ? 0.f : static_cast<float>(completes) / static_cast<float>(m_attempts.size());
    root["SurviveRate"] = surviveRate;
    root["PhysicsHitRate"] = -1.0;
    root["InventRate"] = -1.0;
    root["DAF_exposed"] =
        m_attempts.empty()
            ? 0.f
            : static_cast<float>(EbAssistRealMetrics::get().daf() - m_baselineDaf) /
                  std::max(1, EbAssistRealMetrics::get().arms() - m_baselineArms);
    root["metrics"] = EbAssistRealMetrics::get().reportLine("real-assist");

    auto const dir = EbSaveDropPaths::get().rootDir() / "real_assist";
    auto created = geode::utils::file::createDirectoryAll(dir);
    if (created.isErr()) {
        err = created.unwrapErr();
        return false;
    }
    matjson::Value presses = matjson::Value::array();
    for (auto const& press : m_presses) {
        matjson::Value item = matjson::Value::object();
        item["plannedDelay"] = press.plannedDelay;
        item["planUs"] = press.planUs;
        item["assisted"] = press.assisted;
        item["hybridTried"] = press.hybridTried;
        item["mode"] = press.mode;
        item["attemptTick"] = press.attemptTick;
        item["frame"] = press.frame;
        item["eventIndex"] = press.eventIndex;
        presses.push(item);
    }
    root["presses"] = presses;

    auto const path =
        dir / fmt::format("session_{}_{}_{}_{}.json", m_case.corpusId, m_case.variant,
                          EbStageConfig::assistModeName(m_case.mode), protocolName(m_case.protocol));
    auto written = geode::utils::file::writeString(path, root.dump(2) + "\n");
    if (written.isErr()) {
        err = written.unwrapErr();
        return false;
    }
    return true;
}

std::string EbRealAssistRunner::summaryBlock(bool prepOk) const {
    int completes = 0;
    for (auto const& row : m_attempts) {
        if (row.completed)
            ++completes;
    }
    float survive =
        m_attempts.empty() ? 0.f
                           : static_cast<float>(completes) / static_cast<float>(m_attempts.size());
    auto const& metrics = EbAssistRealMetrics::get();
    int const arms = std::max(0, metrics.arms() - m_baselineArms);
    int const daf = std::max(0, metrics.daf() - m_baselineDaf);
    float const dafExposed = static_cast<float>(daf) / static_cast<float>(std::max(1, arms));
    bool const repro = EbGoldReproduceGate::statusIsPass(m_case.corpusId);
    char const* verdict = "FAIL";
    if (prepOk) {
        if (!repro || m_attempts.empty())
            verdict = "ARMED";
        else
            verdict = "PASS";
    }
    char const* notes = "notes=prep-only-menu-no-level";
    if (!m_lastError.empty())
        notes = m_lastError.c_str();
    else if (m_modesPlayback)
        notes = "notes=modes-live-bot-playback";
    else if (m_levelOpened && m_attempts.empty())
        notes = "notes=waiting-for-PlayLayer-attempts";
    else if (!m_levelOpened)
        notes = "notes=prep-only-set-EB_REAL_ASSIST_OPEN_LEVEL=1-to-enter-level";
    else if (!repro)
        notes = "notes=reproduce-pending-blocks-oracle-and-PASS";
    return fmt::format(
        "=== REAL ASSIST RUNNER {} ===\n"
        "case={}/{}\n"
        "mode={}\n"
        "protocol={}\n"
        "reproduce={}\n"
        "prep={}\n"
        "attempts={}\n"
        "SurviveRate={:.3f}\n"
        "PhysicsHitRate=n/a\n"
        "InventRate=n/a\n"
        "DAF_exposed={:.3f}\n"
        "arms={}\n"
        "levelOpened={}\n"
        "shipLine={} wavePattern={}\n"
        "{}\n",
        verdict, m_case.corpusId, m_case.variant, EbStageConfig::assistModeName(m_case.mode),
        protocolName(m_case.protocol), repro ? "pass" : "pending", prepOk ? "ok" : "fail",
        m_attempts.size(), survive, dafExposed, arms, m_levelOpened, m_case.shipLine,
        m_case.wavePattern, notes);
}

bool EbRealAssistRunner::prepare(CaseSpec const& spec, std::string& report, bool quiet) {
    report.clear();
    if (m_active)
        finish(report);

    m_case = spec;
    m_attempts.clear();
    m_presses.clear();
    m_levelOpened = false;
    m_recordingAttempts = false;
    m_skipSetupReset = false;
    m_skipNextResetRecord = false;
    m_modesPlayback = false;
    m_modesNeedReset = false;
    m_modesDidInitialReset = false;
    m_modesFinishing = false;
    m_playbackEvents.clear();
    m_playbackIndex = 0;
    m_warmPresses.clear();
    m_hybridPresses.clear();
    m_livePresses.clear();
    m_lastError.clear();
    m_sessionPath.clear();
    m_maxPresses = envIntOr(kEnvMaxPresses, 12);
    m_modesTimeWarp = envFloatOr(kEnvTimeWarp, 32.f);
    if (m_maxPresses < 1)
        m_maxPresses = 1;

    std::filesystem::path levelPath;
    std::filesystem::path replayPath;
    std::string err;
    auto publishFail = [&](bool ok) {
        report = summaryBlock(ok);
        if (!quiet) {
            (void)writeLastRun(report, err);
            EbAssistLog::info(report);
            geode::log::info("{}", report);
        }
    };
    if (!resolvePaths(spec, levelPath, replayPath, err)) {
        m_lastError = err;
        publishFail(false);
        return false;
    }
    if (!applyProtocol(spec, err)) {
        m_lastError = err;
        publishFail(false);
        return false;
    }
    if (!importReplayFile(replayPath, err)) {
        m_lastError = err;
        (void)restoreProtocol(err);
        publishFail(false);
        return false;
    }

    m_baselineArms = EbAssistRealMetrics::get().arms();
    m_baselineDaf = EbAssistRealMetrics::get().daf();
    m_baselineOpp = EbAssistRealMetrics::get().nOpp();
    m_active = true;
    m_sessionPath = EbSaveDropPaths::get().rootDir() / "real_assist" /
                    fmt::format("session_{}_{}.json", spec.corpusId, spec.variant);

    (void)spec.shipLine;
    (void)spec.wavePattern;

    report = summaryBlock(true);
    report += fmt::format("level={}\nreplay={}\n", levelPath.string(), replayPath.string());
    (void)writeSession(err);
    if (!quiet) {
        (void)writeLastRun(report, err);
        EbAssistLog::info(report);
        geode::log::info("{}", report);
    }
    return true;
}

bool EbRealAssistRunner::openPreparedLevel(std::string& report) {
    report.clear();
    if (!m_active) {
        report = "no prepared case";
        return false;
    }
#if defined(GEODE_IS_IOS)
    // DS_Dictionary::loadRootSubDictFromFile is not linked in the iOS Geode binary —
    // referencing it fails the dylib link. RealAssist level-open is desktop/CI tooling.
    m_lastError = "RealAssist openPreparedLevel is not supported on iOS";
    report = summaryBlock(false);
    std::string err;
    (void)writeLastRun(report, err);
    return false;
#else
    std::filesystem::path levelPath;
    std::filesystem::path replayPath;
    std::string err;
    if (!resolvePaths(m_case, levelPath, replayPath, err)) {
        m_lastError = err;
        report = summaryBlock(false);
        return false;
    }

    auto* dict = new DS_Dictionary();
    if (!dict->loadRootSubDictFromFile(levelPath.string().c_str())) {
        delete dict;
        m_lastError = fmt::format("DS_Dictionary load failed: {}", levelPath.string());
        report = summaryBlock(false);
        (void)writeLastRun(report, err);
        return false;
    }
    auto* level = GJGameLevel::createWithCoder(dict);
    delete dict;
    if (!level) {
        m_lastError = "GJGameLevel::createWithCoder failed";
        report = summaryBlock(false);
        (void)writeLastRun(report, err);
        return false;
    }
    level->m_levelType = GJLevelType::Editor;
    auto* scene = PlayLayer::scene(level, false, false);
    if (!scene) {
        m_lastError = "PlayLayer::scene failed";
        report = summaryBlock(false);
        (void)writeLastRun(report, err);
        return false;
    }
    cocos2d::CCDirector::sharedDirector()->replaceScene(
        cocos2d::CCTransitionFade::create(0.3f, scene));
    m_levelOpened = true;
    report = summaryBlock(true);
    (void)writeLastRun(report, err);
    EbAssistLog::info(fmt::format("RealAssist opened level for {}/{}", m_case.corpusId,
                                  m_case.variant));
    return true;
#endif
}

bool EbRealAssistRunner::wantOpenLevel() {
    return envTruthy(kEnvOpenLevel);
}

bool EbRealAssistRunner::runCase(CaseSpec const& spec, std::string& report, bool openLevel) {
    if (!prepare(spec, report))
        return false;
    if (!openLevel)
        return true;
    return openPreparedLevel(report);
}

void EbRealAssistRunner::onLevelInit(PlayLayer* layer) {
    if (!m_active || !layer)
        return;
    m_levelOpened = true;
    std::string err;
    (void)applyProtocol(m_case, err);
    m_recordingAttempts = true;
    m_skipSetupReset = true;
    if (s_autoKind == AutoKind::Modes) {
        armModesPlayback();
        // One XDBot-style reset per case — resetLevel must not re-arm forever.
        if (!m_modesDidInitialReset) {
            m_modesNeedReset = true;
            m_prevTimeWarp = layer->m_gameState.m_timeWarp;
            if (m_prevTimeWarp <= 0.f)
                m_prevTimeWarp = 1.f;
            layer->m_gameState.m_timeWarp = m_modesTimeWarp;
        }
        setAssistMasterEnabled(false);
        geode::log::info("RealAssist modes: live bot armed (XDBot-style handleButton + timeWarp)");
    }
}

void EbRealAssistRunner::notePress(int plannedDelay, int planUs, bool hybridTried) {
    if (!m_active || m_modesPlayback)
        return;
    PressRow row;
    row.plannedDelay = plannedDelay;
    row.planUs = planUs;
    row.assisted = true;
    row.hybridTried = hybridTried;
    row.mode = EbStageConfig::assistModeName(m_case.mode);
    row.attemptTick = EbAssistController::get().attemptTick();
    m_presses.push_back(row);
}

void EbRealAssistRunner::setAssistMasterEnabled(bool enabled) {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return;
    try {
        if (!enabled)
            m_prevAssistEnabled = mod->getSettingValue<bool>("enabled");
        mod->setSettingValue<bool>("enabled", enabled);
    } catch (...) {
    }
    EbStageConfig::refreshCache();
}

void EbRealAssistRunner::armModesPlayback() {
    m_modesPlayback = false;
    m_modesFinishing = false;
    m_modesDidInitialReset = false;
    m_modesNeedReset = false;
    m_playbackEvents.clear();
    m_playbackIndex = 0;
    m_warmPresses.clear();
    m_hybridPresses.clear();
    m_livePresses.clear();
    auto const* pending = EbBakeMap::get().pendingReplay();
    if (!pending || pending->events.empty())
        return;
    m_playbackTps = pending->tps > 1.0 ? pending->tps : 240.0;
    m_playbackEvents = pending->events;
    m_modesTimeWarp = envFloatOr(kEnvTimeWarp, 32.f);
    if (m_modesTimeWarp < 1.f)
        m_modesTimeWarp = 1.f;
    if (m_modesTimeWarp > 64.f)
        m_modesTimeWarp = 64.f;
    m_modesPlayback = true;
    geode::log::info("RealAssist modes playback: {} events @ {:.0f} tps warp={:.0f} cap={}",
                     m_playbackEvents.size(), m_playbackTps, m_modesTimeWarp, m_maxPresses);
}

EbSimPlan EbRealAssistRunner::planAtPlayer(PlayLayer* layer, PlayerObject* player, int effectTick,
                                           bool feedLearning) const {
    EbSimPlan plan{};
    if (!layer || !player)
        return plan;

    int const limit = EbStageConfig::paramsFor(player).ticks;
    if (limit <= 0) {
        plan.action = PlanAction::Pass;
        return plan;
    }

    float const simDt = EbAssistController::get().simDt();
    float const deltaTime = simDt > 0.f ? simDt : (1.f / 240.f);
    float const firstDt = deltaTime;
    EbPlanOptions opts;
    opts.attemptStartTick = effectTick;
    opts.legacyPick = EbStageConfig::assistPickLegacy();
    opts.assistEngine = EbStageConfig::effectiveAssistEngine();
    opts.requireAliveForDelay = true;
    // Modes queue: hard-cap plan cost (dual macros otherwise stall for minutes).
    opts.tickBudget = std::min(400, EbStageConfig::tickBudgetPerPress());
    opts.budgetUs = std::min(500, EbStageConfig::simBudgetUs());
    opts.maxBranches = 2;
    opts.smartDepth = 1;
    EbAssistControllerImplementation::fillPlanRobustLatency(opts, player, 0.f, false);

    auto& hybrid = EbHybridLearning::get();
    opts.hybridDelay =
        hybrid.prepare(layer, player, true, true, effectTick, limit, deltaTime, firstDt, opts);
    if (opts.hybridDelay > 0)
        opts.mustDelaySearch = true;

    bool hybridWon = false;
    auto const t0 = std::chrono::steady_clock::now();
    bool const hybridMode = EbStageConfig::assistMode() == EbStageConfig::AssistMode::Hybrid;
    if (hybridMode && hybrid.preferHybridFirst && !layer->m_gameState.m_isDualMode) {
        auto const order = hybrid.rankedVerifyDelays(limit);
        for (int delayValue : order) {
            if (delayValue < 0 || delayValue > limit)
                continue;
            EbPlanOptions vopts = opts;
            vopts.tickBudget = std::max(200, EbStageConfig::tickBudgetPerPress() / 3);
            vopts.maxBranches = 0;
            vopts.smartDepth = 1;
            auto const vr = EbShadowSimulator::get().verifyDelay(layer, player, true, delayValue,
                                                                limit, deltaTime, firstDt, vopts);
            opts.budgetUs = std::max(0, opts.budgetUs - std::max(1, vr.usSpent));
            if (vr.budgetHit || vr.unknown)
                continue;
            if (!vr.ok) {
                hybrid.noteVerifyOutcome(delayValue, false);
                continue;
            }
            hybrid.noteVerifyOutcome(delayValue, true);
            plan.hybridTried = true;
            plan.hybridHint = opts.hybridDelay;
            plan.continuous = player->m_isDart || player->m_isShip || player->m_isSwing;
            plan.fixedFireTick = true;
            if (delayValue <= 0) {
                plan.action = PlanAction::Pass;
                plan.reason = AssistReason::PassNoThreat;
                plan.nowIsSafe = true;
                plan.delay = 0;
            } else {
                plan.action = PlanAction::Delay;
                plan.reason = AssistReason::DelayThreat;
                plan.delay = plan.jumpTick = delayValue;
                plan.viableDelays = std::vector<int>{delayValue};
                plan.dMin = plan.dMax = delayValue;
                plan.guardOriginalDelay = delayValue;
                plan.threatCleared = true;
                plan.nowIsSafe = false;
            }
            hybridWon = true;
            break;
        }
    }
    if (!hybridWon) {
        plan = EbShadowSimulator::get().plan(layer, player, true, limit, deltaTime, firstDt, opts);
    }
    plan.planUs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count());
    plan.hybridHint = opts.hybridDelay;

    if (feedLearning)
        hybrid.decision(true, plan);
    return plan;
}

void EbRealAssistRunner::quitToMenu() {
    m_modesPlayback = false;
    m_modesNeedReset = false;
    m_modesFinishing = false;
    if (auto* layer = PlayLayer::get()) {
        layer->m_gameState.m_timeWarp = m_prevTimeWarp > 0.f ? m_prevTimeWarp : 1.f;
        layer->onQuit();
        return;
    }
    if (auto* scene = MenuLayer::scene(false)) {
        cocos2d::CCDirector::sharedDirector()->replaceScene(
            cocos2d::CCTransitionFade::create(0.2f, scene));
    }
}

bool EbRealAssistRunner::writeLegSession(CaseSpec const& spec, std::vector<PressRow> const& presses,
                                         std::string& err) {
    m_case = spec;
    m_presses = presses;
    m_attempts.clear();
    AttemptRow attempt;
    attempt.index = 0;
    attempt.completed = true;
    attempt.endPercent = 100.f;
    attempt.assistTouched = !presses.empty();
    attempt.arms = static_cast<int>(presses.size());
    double planUsSum = 0.0;
    for (auto const& press : presses)
        planUsSum += static_cast<double>(press.planUs);
    attempt.avgPlanUs =
        presses.empty() ? 0.f
                        : static_cast<float>(planUsSum / static_cast<double>(presses.size()));
    m_attempts.push_back(attempt);
    return writeSession(err);
}

void EbRealAssistRunner::recordPlansAtLivePress(PlayLayer* layer, int frame, int eventIndex) {
    if (!layer || !layer->m_player1)
        return;
    auto* player = layer->m_player1;
    CaseSpec const base = m_case;
    std::string err;
    geode::log::info("RealAssist modes plan@frame={} x={:.1f} queue={}/{}", frame,
                     player->getPositionX(), s_modesQueueIndex + 1, s_modesQueue.size());

    auto makeRow = [](EbSimPlan const& plan, EbStageConfig::AssistMode mode, int frameValue,
                      int eventIndexValue) {
        PressRow row;
        row.plannedDelay = plan.action == PlanAction::Delay ? std::max(0, plan.delay) : 0;
        row.planUs = plan.planUs;
        row.assisted = plan.action == PlanAction::Delay;
        row.hybridTried = plan.hybridTried;
        row.mode = EbStageConfig::assistModeName(mode);
        row.attemptTick = frameValue;
        row.frame = frameValue;
        row.eventIndex = eventIndexValue;
        return row;
    };

    // Live first (pure search), seed Hybrid evidence, then Frozen Hybrid — skip Online warm
    // full-plan (was 3× plan cost; dual levels stalled minutes on first press).
    CaseSpec liveSpec = base;
    liveSpec.mode = EbStageConfig::AssistMode::Live;
    liveSpec.protocol = Protocol::Frozen;
    EbSimPlan livePlan{};
    if (applyProtocol(liveSpec, err))
        livePlan = planAtPlayer(layer, player, frame, false);
    m_livePresses.push_back(makeRow(livePlan, liveSpec.mode, frame, eventIndex));

    int const liveDelay =
        livePlan.action == PlanAction::Delay ? std::max(0, livePlan.delay) : 0;

    CaseSpec warmSpec = base;
    warmSpec.mode = EbStageConfig::AssistMode::Hybrid;
    warmSpec.protocol = Protocol::Online;
    if (applyProtocol(warmSpec, err)) {
        // Cheap warm: prepare key + seed verify only (no second Live search).
        (void)EbHybridLearning::get().prepare(layer, player, true, true, frame,
                                              EbStageConfig::paramsFor(player).ticks,
                                              EbAssistController::get().simDt() > 0.f
                                                  ? EbAssistController::get().simDt()
                                                  : (1.f / 240.f),
                                              1.f / 240.f, EbPlanOptions{});
        EbHybridLearning::get().noteVerifyOutcome(liveDelay, true);
        PressRow warmRow = makeRow(livePlan, warmSpec.mode, frame, eventIndex);
        warmRow.hybridTried = false;
        m_warmPresses.push_back(warmRow);
    }

    CaseSpec hybridSpec = base;
    hybridSpec.mode = EbStageConfig::AssistMode::Hybrid;
    hybridSpec.protocol = Protocol::Frozen;
    EbSimPlan hybridPlan{};
    if (applyProtocol(hybridSpec, err))
        hybridPlan = planAtPlayer(layer, player, frame, false);
    m_hybridPresses.push_back(makeRow(hybridPlan, hybridSpec.mode, frame, eventIndex));

    m_case = base;
    setAssistMasterEnabled(false);
}

void EbRealAssistRunner::finishModesCompare(std::string const& reason) {
    if (m_modesFinishing)
        return;
    m_modesFinishing = true;
    m_modesPlayback = false;
    m_modesNeedReset = false;

    CaseSpec const base = m_case;
    std::string err;
    std::string report;
    bool const ok = !(m_livePresses.empty() && m_hybridPresses.empty());

    if (!ok) {
        m_lastError = fmt::format("modes: no presses planned ({})", reason);
        report = summaryBlock(false);
        report += fmt::format("modesFail={}\n", m_lastError);
        (void)writeLastRun(report, err);
        geode::log::warn("{}", report);
        s_modesQueueResults.push_back(
            fmt::format("FAIL {}/{} reason={}", base.corpusId, base.variant, reason));
    } else {
        CaseSpec warmSpec = base;
        warmSpec.mode = EbStageConfig::AssistMode::Hybrid;
        warmSpec.protocol = Protocol::Online;
        CaseSpec hybridSpec = base;
        hybridSpec.mode = EbStageConfig::AssistMode::Hybrid;
        hybridSpec.protocol = Protocol::Frozen;
        CaseSpec liveSpec = base;
        liveSpec.mode = EbStageConfig::AssistMode::Live;
        liveSpec.protocol = Protocol::Frozen;
        (void)writeLegSession(warmSpec, m_warmPresses, err);
        (void)writeLegSession(hybridSpec, m_hybridPresses, err);
        (void)writeLegSession(liveSpec, m_livePresses, err);
        m_case = base;
        (void)writeModeCompare(err);

        int delayMismatch = 0;
        size_t const compared = std::min(m_livePresses.size(), m_hybridPresses.size());
        for (size_t index = 0; index < compared; ++index) {
            if (m_livePresses[index].plannedDelay != m_hybridPresses[index].plannedDelay)
                ++delayMismatch;
        }

        report = fmt::format(
            "=== REAL ASSIST MODES OK ===\n"
            "case={}/{}\n"
            "queue={}/{}\n"
            "presses={}\n"
            "timeWarp={:.0f}\n"
            "delayMismatch={}/{}\n"
            "reason={}\n"
            "wrote session_* + mode_compare_*.json\n",
            base.corpusId, base.variant, s_modesQueueIndex + 1, s_modesQueue.size(), compared,
            m_modesTimeWarp, delayMismatch, compared, reason);
        (void)writeLastRun(report, err);
        EbAssistLog::info(report);
        geode::log::info("{}", report);
        s_modesQueueResults.push_back(fmt::format(
            "PASS {}/{} presses={} delayMismatch={}/{} reason={}", base.corpusId, base.variant,
            compared, delayMismatch, compared, reason));
    }

    setAssistMasterEnabled(m_prevAssistEnabled);
    finishQuiet();

    ++s_modesQueueIndex;
    // Never open/quit from inside onPhysicsTick — defer to the next main-thread turn.
    auto* loader = geode::Loader::get();
    auto cont = [] {
        if (s_modesQueueIndex < s_modesQueue.size()) {
            // Stay in-game: replaceScene to the next corpus (menu hop raced onQuit and
            // killed the freshly opened PlayLayer — dual levels then never ticked).
            s_autoKind = AutoKind::Modes;
            s_modesAwaitMenu = false;
            geode::log::info("RealAssist modes queue: next {}/{} → {}/{}", s_modesQueueIndex + 1,
                             s_modesQueue.size(), s_modesQueue[s_modesQueueIndex].corpusId,
                             s_modesQueue[s_modesQueueIndex].variant);
            std::string report;
            (void)get().advanceModesQueueFromMenu(report);
            return;
        }
        std::string err;
        (void)get().writeModesQueueSummary(err);
        s_autoKind = AutoKind::None;
        s_modesAwaitMenu = false;
        get().quitToMenu();
    };
    if (!loader) {
        cont();
        return;
    }
    loader->queueInMainThread(cont);
}

void EbRealAssistRunner::onPhysicsTick(PlayLayer* layer) {
    if (!m_active || !layer || !m_modesPlayback || m_modesFinishing)
        return;

    int const tick = EbAssistController::get().attemptTick();

    // XDBot: first attempt of a bot macro restarts so frame/input alignment is clean.
    if (m_modesNeedReset && tick > 2) {
        m_modesNeedReset = false;
        m_modesDidInitialReset = true;
        m_skipSetupReset = true;
        m_playbackIndex = 0;
        m_prevTimeWarp = layer->m_gameState.m_timeWarp;
        if (m_prevTimeWarp <= 0.f)
            m_prevTimeWarp = 1.f;
        layer->m_gameState.m_timeWarp = m_modesTimeWarp;
        layer->resetLevel();
        geode::log::info("RealAssist modes: resetLevel + timeWarp={:.0f}", m_modesTimeWarp);
        return;
    }
    if (m_modesNeedReset)
        return;

    // Keep warp stuck on after reset (some paths restore 1.0).
    if (layer->m_gameState.m_timeWarp < m_modesTimeWarp * 0.5f)
        layer->m_gameState.m_timeWarp = m_modesTimeWarp;

    if (layer->m_player1 && layer->m_player1->m_isDead && tick > 5) {
        finishModesCompare("player-dead");
        return;
    }

    // Dual/long macros: don't hang forever waiting for Jump-downs.
    if (m_livePresses.empty() && tick > 20000) {
        finishModesCompare("timeout-no-press");
        return;
    }
    if (!m_livePresses.empty() && tick > 60000) {
        finishModesCompare("timeout-partial");
        return;
    }

    double const scale = 240.0 / m_playbackTps;
    while (m_playbackIndex < m_playbackEvents.size()) {
        auto const& event = m_playbackEvents[m_playbackIndex];
        int const target =
            static_cast<int>(std::llround(static_cast<double>(event.frame) * scale));
        if (target > tick)
            break;

        bool const jumpDown = event.button == 1 && event.down && !event.player2;
        if (jumpDown && static_cast<int>(m_livePresses.size()) < m_maxPresses) {
            recordPlansAtLivePress(layer, event.frame, static_cast<int>(m_playbackIndex));
            size_t const n = m_livePresses.size();
            if (n == 1 || n % 4 == 0 || static_cast<int>(n) >= m_maxPresses) {
                geode::log::info("RealAssist modes press {}/{} frame={} L/H={}/{}", n, m_maxPresses,
                                 event.frame,
                                 n ? m_livePresses.back().plannedDelay : -1,
                                 n && !m_hybridPresses.empty() ? m_hybridPresses.back().plannedDelay
                                                               : -1);
            }
        }

        if (event.button >= 1 && event.button <= 3)
            layer->handleButton(event.down, event.button, !event.player2);
        ++m_playbackIndex;

        if (static_cast<int>(m_livePresses.size()) >= m_maxPresses) {
            finishModesCompare("max-presses");
            return;
        }
    }

    if (m_playbackIndex >= m_playbackEvents.size())
        finishModesCompare("replay-end");
}

void EbRealAssistRunner::onAttemptEnd(PlayLayer* layer, bool completed, float endPercent) {
    if (!m_active || !layer)
        return;
    if (m_modesPlayback || m_modesFinishing || s_autoKind == AutoKind::Modes) {
        if (m_modesPlayback && !m_modesFinishing && !completed && !m_skipSetupReset)
            finishModesCompare("attempt-end");
        if (!completed && m_skipSetupReset)
            m_skipSetupReset = false;
        return;
    }
    if (!m_recordingAttempts)
        return;
    if (!completed && m_skipSetupReset) {
        m_skipSetupReset = false;
        return;
    }
    if (!completed && m_skipNextResetRecord) {
        m_skipNextResetRecord = false;
        return;
    }
    if (completed)
        m_skipNextResetRecord = true;
    AttemptRow row;
    row.index = static_cast<int>(m_attempts.size());
    row.completed = completed;
    row.endPercent = endPercent;
    row.arms = std::max(0, EbAssistRealMetrics::get().arms() - m_baselineArms);
    row.daf = std::max(0, EbAssistRealMetrics::get().daf() - m_baselineDaf);
    row.nOpp = std::max(0, EbAssistRealMetrics::get().nOpp() - m_baselineOpp);
    row.budgetHitRate = EbAssistPerfStats::get().budgetHitRate();
    row.avgPlanUs = EbAssistPerfStats::get().avgMicros();
    row.assistTouched = EbAssistController::get().assistTouchedRun();
    m_attempts.push_back(row);

    std::string err;
    (void)writeSession(err);
    auto block = summaryBlock(true);
    (void)writeLastRun(block, err);
    EbAssistLog::info(fmt::format("RealAssist attempt#{} complete={} pct={:.1f} presses={}",
                                  row.index, completed, endPercent, m_presses.size()));
    geode::log::info("{}", block);
}

void EbRealAssistRunner::finishQuiet() {
    std::string err;
    (void)restoreProtocol(err);
    if (auto* layer = PlayLayer::get()) {
        if (m_prevTimeWarp > 0.f)
            layer->m_gameState.m_timeWarp = m_prevTimeWarp;
    }
    m_active = false;
    m_levelOpened = false;
    m_recordingAttempts = false;
    m_skipSetupReset = false;
    m_modesPlayback = false;
    m_modesNeedReset = false;
    m_modesDidInitialReset = false;
    m_modesFinishing = false;
}

void EbRealAssistRunner::finish(std::string& report) {
    m_modesPlayback = false;
    m_modesNeedReset = false;
    m_modesDidInitialReset = false;
    m_modesFinishing = false;
    std::string err;
    (void)writeSession(err);
    (void)restoreProtocol(err);
    report = summaryBlock(true);
    (void)writeLastRun(report, err);
    m_active = false;
    m_levelOpened = false;
    m_recordingAttempts = false;
    m_skipSetupReset = false;
}

int EbRealAssistRunner::envIntOr(char const* name, int fallback) {
    auto text = envOrEmpty(name);
    if (text.empty())
        return fallback;
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

float EbRealAssistRunner::envFloatOr(char const* name, float fallback) {
    auto text = envOrEmpty(name);
    if (text.empty())
        return fallback;
    try {
        return std::stof(text);
    } catch (...) {
        return fallback;
    }
}

bool EbRealAssistRunner::writeModeCompare(std::string& err) const {
    err.clear();
    auto const dir = EbSaveDropPaths::get().rootDir() / "real_assist";
    auto created = geode::utils::file::createDirectoryAll(dir);
    if (created.isErr()) {
        err = created.unwrapErr();
        return false;
    }
    auto readSession = [&](char const* mode, char const* protocol) -> matjson::Value {
        auto path = dir / fmt::format("session_{}_{}_{}_{}.json", m_case.corpusId, m_case.variant,
                                      mode, protocol);
        auto parsed = geode::utils::file::readJson(path);
        if (parsed.isErr())
            return matjson::Value::object();
        return parsed.unwrap();
    };
    auto live = readSession("Live", "Frozen");
    auto hybrid = readSession("Hybrid", "Frozen");
    auto warm = readSession("Hybrid", "Online");
    matjson::Value root = matjson::Value::object();
    root["schema"] = 1;
    root["corpusId"] = m_case.corpusId;
    root["variant"] = m_case.variant;
    root["modesLiveBot"] = true;
    root["LiveFrozen"] = live;
    root["HybridFrozen"] = hybrid;
    root["HybridOnlineWarm"] = warm;

    auto avgPlan = [](matjson::Value const& session) -> double {
        if (!session.contains("attempts") || !session["attempts"].isArray())
            return -1.0;
        double sum = 0.0;
        int n = 0;
        for (auto const& row : session["attempts"]) {
            if (!row.contains("avgPlanUs"))
                continue;
            sum += row["avgPlanUs"].asDouble().unwrapOr(0.0);
            ++n;
        }
        return n ? sum / static_cast<double>(n) : -1.0;
    };
    auto pressCount = [](matjson::Value const& session) -> int {
        if (!session.contains("presses") || !session["presses"].isArray())
            return 0;
        return static_cast<int>(session["presses"].size());
    };
    auto hybridTried = [](matjson::Value const& session) -> int {
        if (!session.contains("presses") || !session["presses"].isArray())
            return 0;
        int n = 0;
        for (auto const& row : session["presses"]) {
            if (row.contains("hybridTried") && row["hybridTried"].asBool().unwrapOr(false))
                ++n;
        }
        return n;
    };
    auto delayAt = [](matjson::Value const& session, size_t index) -> int {
        if (!session.contains("presses") || !session["presses"].isArray())
            return 0;
        auto const& presses = session["presses"];
        if (index >= presses.size())
            return 0;
        return presses[index]["plannedDelay"].asInt().unwrapOr(0);
    };

    int const compared = std::min(pressCount(live), pressCount(hybrid));
    int delayMismatch = 0;
    int hybridTriedOnlyHybrid = 0;
    matjson::Value delayPairs = matjson::Value::array();
    for (int index = 0; index < compared; ++index) {
        int const liveDelay = delayAt(live, static_cast<size_t>(index));
        int const hybridDelay = delayAt(hybrid, static_cast<size_t>(index));
        if (liveDelay != hybridDelay)
            ++delayMismatch;
        bool const hTried =
            hybrid.contains("presses") && hybrid["presses"].isArray() &&
            static_cast<size_t>(index) < hybrid["presses"].size() &&
            hybrid["presses"][static_cast<size_t>(index)]["hybridTried"].asBool().unwrapOr(false);
        bool const lTried =
            live.contains("presses") && live["presses"].isArray() &&
            static_cast<size_t>(index) < live["presses"].size() &&
            live["presses"][static_cast<size_t>(index)]["hybridTried"].asBool().unwrapOr(false);
        if (hTried && !lTried)
            ++hybridTriedOnlyHybrid;
        if (index < 32 || liveDelay != hybridDelay) {
            matjson::Value pair = matjson::Value::object();
            pair["i"] = index;
            pair["live"] = liveDelay;
            pair["hybrid"] = hybridDelay;
            delayPairs.push(pair);
        }
    }

    root["diff"] = matjson::Value::object();
    root["diff"]["avgPlanUs_HybridMinusLive"] = avgPlan(hybrid) - avgPlan(live);
    root["diff"]["presses_Live"] = pressCount(live);
    root["diff"]["presses_Hybrid"] = pressCount(hybrid);
    root["diff"]["presses_compared"] = compared;
    root["diff"]["delayMismatch"] = delayMismatch;
    root["diff"]["hybridTried_Hybrid"] = hybridTried(hybrid);
    root["diff"]["hybridTried_Live"] = hybridTried(live);
    root["diff"]["hybridTriedOnlyHybrid"] = hybridTriedOnlyHybrid;
    root["diff"]["warmPresses"] = pressCount(warm);
    root["diff"]["delayPairs"] = delayPairs;

    auto const dump = root.dump(2) + "\n";
    auto const perCase =
        dir / fmt::format("mode_compare_{}_{}.json", m_case.corpusId, m_case.variant);
    auto writtenCase = geode::utils::file::writeString(perCase, dump);
    if (writtenCase.isErr()) {
        err = writtenCase.unwrapErr();
        return false;
    }
    auto writtenLatest = geode::utils::file::writeString(dir / "mode_compare.json", dump);
    if (writtenLatest.isErr()) {
        err = writtenLatest.unwrapErr();
        return false;
    }
    geode::log::info(
        "RealAssist mode_compare {}/{}: delayMismatch={}/{} planUsΔ={:.1f} "
        "hybridTried L/H={}/{} onlyH={}",
        m_case.corpusId, m_case.variant, delayMismatch, compared,
        avgPlan(hybrid) - avgPlan(live), hybridTried(live), hybridTried(hybrid),
        hybridTriedOnlyHybrid);
    return true;
}

bool EbRealAssistRunner::writeModesQueueSummary(std::string& err) const {
    err.clear();
    auto const dir = EbSaveDropPaths::get().rootDir() / "real_assist";
    auto created = geode::utils::file::createDirectoryAll(dir);
    if (created.isErr()) {
        err = created.unwrapErr();
        return false;
    }
    matjson::Value root = matjson::Value::object();
    root["schema"] = 1;
    root["queueSize"] = static_cast<int>(s_modesQueue.size());
    matjson::Value results = matjson::Value::array();
    int fails = 0;
    for (auto const& line : s_modesQueueResults) {
        results.push(line);
        if (line.rfind("FAIL", 0) == 0)
            ++fails;
    }
    root["results"] = results;
    root["fails"] = fails;
    matjson::Value cases = matjson::Value::array();
    for (auto const& spec : s_modesQueue) {
        matjson::Value item = matjson::Value::object();
        item["corpusId"] = spec.corpusId;
        item["variant"] = spec.variant;
        auto path = dir / fmt::format("mode_compare_{}_{}.json", spec.corpusId, spec.variant);
        auto parsed = geode::utils::file::readJson(path);
        if (parsed.isOk())
            item["compare"] = parsed.unwrap();
        cases.push(item);
    }
    root["cases"] = cases;
    auto written =
        geode::utils::file::writeString(dir / "mode_compare_all.json", root.dump(2) + "\n");
    if (written.isErr()) {
        err = written.unwrapErr();
        return false;
    }
    std::string summary = fmt::format(
        "=== REAL ASSIST MODES QUEUE DONE ===\n"
        "cases={}\n"
        "fails={}\n",
        s_modesQueue.size(), fails);
    for (auto const& line : s_modesQueueResults)
        summary += line + "\n";
    summary += "wrote mode_compare_all.json\n";
    (void)writeLastRun(summary, err);
    EbAssistLog::info(summary);
    geode::log::info("{}", summary);
    return true;
}

bool EbRealAssistRunner::advanceModesQueueFromMenu(std::string& report) {
    report.clear();
    if (s_modesQueueIndex >= s_modesQueue.size()) {
        std::string err;
        (void)writeModesQueueSummary(err);
        report = "modes queue complete";
        s_autoKind = AutoKind::None;
        s_modesAwaitMenu = false;
        return true;
    }
    CaseSpec spec = s_modesQueue[s_modesQueueIndex];
    geode::log::info("RealAssist modes queue {}/{}: open {}/{} then live bot+timeWarp",
                     s_modesQueueIndex + 1, s_modesQueue.size(), spec.corpusId, spec.variant);
    return runCase(spec, report, /*openLevel=*/true);
}

bool EbRealAssistRunner::runModesFromMenu(std::string& report) {
    report.clear();
    auto caseEnv = envOrEmpty(kEnvCase);
    for (char& character : caseEnv) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }

    s_modesQueue.clear();
    s_modesQueueIndex = 0;
    s_modesQueueResults.clear();
    s_modesAwaitMenu = false;

    // Default / all / matrix → every smoke corpus. Single id or id/variant → one case.
    if (caseEnv.empty() || caseEnv == "all" || caseEnv == "matrix" || caseEnv == "modes") {
        s_modesQueue = modesCaseQueue();
    } else {
        CaseSpec spec;
        std::string err;
        if (!parseCaseString(caseEnv, spec, err)) {
            report = err;
            return false;
        }
        spec.mode = EbStageConfig::AssistMode::Hybrid;
        spec.protocol = Protocol::Online;
        s_modesQueue.push_back(spec);
    }
    if (s_modesQueue.empty()) {
        report = "modes queue empty";
        return false;
    }
    geode::log::info("RealAssist modes queue armed: {} case(s)", s_modesQueue.size());
    return advanceModesQueueFromMenu(report);
}

bool EbRealAssistRunner::runDevAction(std::string& report) {
    report.clear();
    auto autoVal = envOrEmpty(kEnvAuto);
    for (char& character : autoVal) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    if (autoVal == "modes" || autoVal == "mode-compare" || autoVal == "compare-modes") {
        s_autoKind = AutoKind::Modes;
        return runModesFromMenu(report);
    }
    auto caseEnv = envOrEmpty(kEnvCase);
    if (caseEnv.empty()) {
        auto* mod = geode::prelude::Mod::get();
        if (mod) {
            try {
                caseEnv = mod->getSettingValue<std::string>("real-assist-case");
            } catch (...) {
            }
        }
    }

    CaseSpec spec;
    std::string err;
    Protocol protocol = Protocol::Frozen;
    EbStageConfig::AssistMode mode = EbStageConfig::AssistMode::Live;
    (void)parseProtocol(envOrEmpty(kEnvProtocol), protocol);
    (void)parseMode(envOrEmpty(kEnvMode), mode);
    if (auto* mod = geode::prelude::Mod::get()) {
        try {
            std::string protoSet = mod->getSettingValue<std::string>("real-assist-protocol");
            (void)parseProtocol(protoSet, protocol);
        } catch (...) {
        }
        try {
            std::string modeSet = mod->getSettingValue<std::string>("real-assist-mode");
            (void)parseMode(modeSet, mode);
        } catch (...) {
        }
    }

    bool const wantMatrix =
        autoVal == "matrix" || caseEnv.empty() || caseEnv == "matrix" || caseEnv == "all";
    if (wantMatrix && (autoVal == "matrix" || caseEnv == "matrix" || caseEnv == "all" ||
                       caseEnv.empty())) {
        auto rows = smokeMatrix();
        int fails = 0;
        std::string lines = "=== REAL ASSIST MATRIX (prep) ===\n";
        for (auto row : rows) {
            row.protocol = Protocol::Frozen;
            std::string one;
            if (!prepare(row, one, /*quiet=*/true)) {
                ++fails;
                lines += fmt::format("FAIL {}/{} {}\n", row.corpusId, row.variant,
                                     EbStageConfig::assistModeName(row.mode));
            } else {
                lines += fmt::format("PASS {}/{} {}\n", row.corpusId, row.variant,
                                     EbStageConfig::assistModeName(row.mode));
            }
            std::string restoreErr;
            (void)restoreProtocol(restoreErr);
            m_active = false;
        }
        lines += fmt::format("prepFails={}/{}\n", fails, rows.size());
        report = lines;
        (void)writeLastRun(report, err);
        EbAssistLog::info(report);
        if (fails)
            geode::log::warn("{}", report);
        else
            geode::log::info("{}", report);
        return fails == 0;
    }

    if (!parseCaseString(caseEnv, spec, err)) {
        report = err;
        return false;
    }
    spec.mode = mode;
    spec.protocol = protocol;

    std::string importReport;
    (void)EbGoldReproduceGate::importAndStamp(spec.corpusId, importReport);

    bool const ok = runCase(spec, report, wantOpenLevel());
    return ok;
}

void EbRealAssistRunner::maybeQueueAutoFromEnv() {
    auto const autoVal = envOrEmpty(kEnvAuto);
    if (autoVal.empty())
        return;
    s_autoArmed = true;
    s_autoFired = false;
    s_modesAwaitMenu = false;
    std::string lower = autoVal;
    for (char& character : lower) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    if (lower == "modes" || lower == "mode-compare" || lower == "compare-modes")
        s_autoKind = AutoKind::Modes;
    else
        s_autoKind = AutoKind::MatrixOrCase;
    geode::log::info("RealAssist auto armed (EB_REAL_ASSIST_AUTO={})", autoVal);
}

void EbRealAssistRunner::onMenuLayerReady() {
    auto* loader = geode::Loader::get();
    auto kick = [](bool queueAdvance) {
        auto run = [queueAdvance] {
            std::string report;
            if (queueAdvance)
                (void)get().advanceModesQueueFromMenu(report);
            else if (s_autoKind == AutoKind::Modes)
                (void)get().runModesFromMenu(report);
            else
                (void)get().runDevAction(report);
        };
        auto* activeLoader = geode::Loader::get();
        if (!activeLoader) {
            run();
            return;
        }
        activeLoader->queueInMainThread(run);
    };

    if (s_autoKind == AutoKind::Modes && s_modesAwaitMenu) {
        s_modesAwaitMenu = false;
        // Defer past quit→menu scene settle so the next open isn't racing PlayLayer teardown.
        auto* activeLoader = geode::Loader::get();
        auto advance = [] {
            auto* nested = geode::Loader::get();
            auto run = [] {
                std::string report;
                (void)get().advanceModesQueueFromMenu(report);
            };
            if (!nested) {
                run();
                return;
            }
            nested->queueInMainThread(run);
        };
        if (!activeLoader) {
            advance();
            return;
        }
        activeLoader->queueInMainThread(advance);
        return;
    }
    if (!s_autoArmed || s_autoFired)
        return;
    s_autoFired = true;
    s_autoArmed = false;
    (void)loader;
    kick(false);
}

bool EbRealAssistRunner::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string(name) + "; ";
        }
    };
    expect(std::string(kEnvAuto) == "EB_REAL_ASSIST_AUTO", "env-auto");
    expect(std::string(kEnvOpenLevel) == "EB_REAL_ASSIST_OPEN_LEVEL", "env-open");
    expect(std::string(kEnvMaxPresses) == "EB_REAL_ASSIST_MAX_PRESSES", "env-max-presses");
    expect(std::string(kEnvTimeWarp) == "EB_REAL_ASSIST_TIME_WARP", "env-time-warp");
    expect(!wantOpenLevel() || envTruthy(kEnvOpenLevel), "open-default-off");
    expect(std::string(protocolName(Protocol::Frozen)) == "Frozen", "proto-frozen");
    expect(std::string(protocolName(Protocol::Online)) == "Online", "proto-online");
    Protocol protocol = Protocol::Frozen;
    expect(parseProtocol("online", protocol) && protocol == Protocol::Online, "parse-online");
    EbStageConfig::AssistMode mode = EbStageConfig::AssistMode::Live;
    expect(parseMode("Hybrid", mode) && mode == EbStageConfig::AssistMode::Hybrid, "parse-hybrid");
    expect(parseMode("AI", mode) && mode == EbStageConfig::AssistMode::AI, "parse-ai");
    CaseSpec spec;
    std::string err;
    expect(parseCaseString("cataclysm/pulse-shift-early-2", spec, err) &&
               spec.corpusId == "cataclysm" && spec.variant == "pulse-shift-early-2",
           "parse-case");
    expect(smokeMatrix().size() == 16, "matrix-16");
    expect(modesCaseQueue().size() == 4, "modes-queue-4");
    if (fails == 0)
        report = "RealAssistRunner OK";
    else
        report = fmt::format("RealAssistRunner {} fails: {}", fails, report);
    return fails == 0;
}
