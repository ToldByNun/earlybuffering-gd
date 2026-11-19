#include "real_assist_runner.hpp"
#include "../../debug/assist_log.hpp"
#include "../assist/assist_controller.hpp"
#include "../bake/bake_map.hpp"
#include "../diagnostics/assist_perf_stats.hpp"
#include "../diagnostics/assist_real_metrics.hpp"
#include "../learning/hybrid_learning.hpp"
#include "../platform/save_drop_paths.hpp"
#include "../replay/replay_import.hpp"
#include "gold_reproduce_gate.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cstdlib>
#include <system_error>

bool EbRealAssistRunner::s_autoArmed = false;
bool EbRealAssistRunner::s_autoFired = false;

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
    // Prefer exact stem, then stem__c*.gdr2, then fixture-tree derived under tools if save copy
    // only has golds.
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(derived / (spec.variant + ".gdr2"));
    if (std::filesystem::is_directory(derived, ec)) {
        for (auto const& entry : std::filesystem::directory_iterator(derived, ec)) {
            if (ec || !entry.is_regular_file())
                continue;
            auto name = entry.path().filename().string();
            if (name.rfind(spec.variant, 0) == 0 && name.size() > 5 &&
                name.size() >= 5 && name.compare(name.size() - 5, 5, ".gdr2") == 0) {
                candidates.push_back(entry.path());
            }
        }
    }
    // Fixture tree fallback (dev machine).
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

    if (spec.protocol == Protocol::Frozen) {
        try {
            mod->setSettingValue<bool>("auto-tune", false);
        } catch (...) {
        }
        EbStageConfig::refreshCache();
        EbHybridLearning::get().freeze(true);
    } else {
        EbHybridLearning::get().reset();
        try {
            mod->setSettingValue<bool>("auto-tune", true);
        } catch (...) {
        }
        EbStageConfig::refreshCache();
        EbHybridLearning::get().freeze(false);
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
    // Stage into replay-drop for manual re-import.
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
    root["PhysicsHitRate"] = -1.0; // filled by APR3 scorer against PhysicsSafeSet
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
    auto const path =
        dir / fmt::format("session_{}_{}_{}.json", m_case.corpusId, m_case.variant,
                          protocolName(m_case.protocol));
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
    // PASS only after prep + reproduce=pass + at least one real attempt.
    // Prep/open alone is ARMED (not acceptance).
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
    m_levelOpened = false;
    m_recordingAttempts = false;
    m_skipSetupReset = false;
    m_skipNextResetRecord = false;
    m_lastError.clear();
    m_sessionPath.clear();

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
    // Re-assert protocol after level profile load may refresh settings.
    std::string err;
    (void)applyProtocol(m_case, err);
    // First resetAttempt after init is setup, not a death — ignore it.
    m_recordingAttempts = true;
    m_skipSetupReset = true;
}

void EbRealAssistRunner::onAttemptEnd(PlayLayer* layer, bool completed, float endPercent) {
    if (!m_active || !layer)
        return;
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
    EbAssistLog::info(fmt::format("RealAssist attempt#{} complete={} pct={:.1f}", row.index,
                                  completed, endPercent));
    geode::log::info("{}", block);
}

void EbRealAssistRunner::finish(std::string& report) {
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

bool EbRealAssistRunner::runDevAction(std::string& report) {
    report.clear();
    auto autoVal = envOrEmpty(kEnvAuto);
    for (char& character : autoVal) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
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
        // Prep-only matrix smoke (does not open every level).
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
            // Quiet restore — avoid per-row ARMED last_run spam.
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

    // Stamp import half of reproduce gate for this id (non-fatal if missing).
    std::string importReport;
    (void)EbGoldReproduceGate::importAndStamp(spec.corpusId, importReport);

    bool const ok = runCase(spec, report, wantOpenLevel());
    return ok;
}

void EbRealAssistRunner::maybeQueueAutoFromEnv() {
    auto const autoVal = envOrEmpty(kEnvAuto);
    if (autoVal.empty())
        return;
    // Arm only — fire from MenuLayer::init so we survive LoadingLayer teardown.
    s_autoArmed = true;
    s_autoFired = false;
    geode::log::info("RealAssist auto armed (EB_REAL_ASSIST_AUTO={})", autoVal);
}

void EbRealAssistRunner::onMenuLayerReady() {
    if (!s_autoArmed || s_autoFired)
        return;
    s_autoFired = true;
    s_autoArmed = false;
    // Defer one frame so MenuLayer finishes presenting before prep I/O.
    auto* loader = geode::Loader::get();
    if (!loader) {
        std::string report;
        (void)get().runDevAction(report);
        return;
    }
    loader->queueInMainThread([] {
        std::string report;
        (void)get().runDevAction(report);
    });
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
    expect(!wantOpenLevel() || envTruthy(kEnvOpenLevel), "open-default-off");
    expect(std::string(protocolName(Protocol::Frozen)) == "Frozen", "proto-frozen");
    expect(std::string(protocolName(Protocol::Online)) == "Online", "proto-online");
    Protocol protocol = Protocol::Frozen;
    expect(parseProtocol("online", protocol) && protocol == Protocol::Online, "parse-online");
    EbStageConfig::AssistMode mode = EbStageConfig::AssistMode::Live;
    expect(parseMode("Hybrid", mode) && mode == EbStageConfig::AssistMode::Hybrid, "parse-hybrid");
    CaseSpec spec;
    std::string err;
    expect(parseCaseString("cataclysm/pulse-shift-early-2", spec, err) &&
               spec.corpusId == "cataclysm" && spec.variant == "pulse-shift-early-2",
           "parse-case");
    expect(smokeMatrix().size() == 16, "matrix-16");
    if (fails == 0)
        report = "RealAssistRunner OK";
    else
        report = fmt::format("RealAssistRunner {} fails: {}", fails, report);
    return fails == 0;
}
