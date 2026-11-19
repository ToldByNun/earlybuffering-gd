#ifndef REAL_ASSIST_RUNNER_HPP
#define REAL_ASSIST_RUNNER_HPP

#include "../../config/stage_config.hpp"
#include <filesystem>
#include <string>
#include <vector>

class PlayLayer;

// APR2: in-game Real Assist Runner (Frozen / Online) + geode-run auto-driver.
class EbRealAssistRunner {
public:
    enum class Protocol { Frozen = 0, Online };

    class CaseSpec {
    public:
        std::string corpusId;
        std::string variant; // e.g. on-time, pulse-shift-early-2
        EbStageConfig::AssistMode mode = EbStageConfig::AssistMode::Live;
        Protocol protocol = Protocol::Frozen;
        bool shipLine = false;
        bool wavePattern = false;

    private:
    };

    class AttemptRow {
    public:
        int index = 0;
        bool completed = false;
        float endPercent = 0.f;
        int arms = 0;
        int daf = 0;
        int nOpp = 0;
        float budgetHitRate = 0.f;
        float avgPlanUs = 0.f;
        bool assistTouched = false;

    private:
    };

    static EbRealAssistRunner& get();

    static constexpr char const* kEnvAuto = "EB_REAL_ASSIST_AUTO";
    static constexpr char const* kEnvCase = "EB_REAL_ASSIST_CASE";
    static constexpr char const* kEnvMode = "EB_REAL_ASSIST_MODE";
    static constexpr char const* kEnvProtocol = "EB_REAL_ASSIST_PROTOCOL";
    // Opt-in only: default runner stays on the menu (prep/import, no PlayLayer scene).
    static constexpr char const* kEnvOpenLevel = "EB_REAL_ASSIST_OPEN_LEVEL";

    static char const* protocolName(Protocol protocol);
    static bool parseProtocol(std::string const& text, Protocol& out);
    static bool parseMode(std::string const& text, EbStageConfig::AssistMode& out);
    static bool parseCaseString(std::string const& text, CaseSpec& out, std::string& err);

    // Smoke matrix: 4 ids × {Live,Hybrid} × {on-time, pulse-shift-early-2} × Frozen.
    static std::vector<CaseSpec> smokeMatrix();

    bool active() const;
    CaseSpec const& current() const;

    // Resolve files, apply mode/protocol, import shifted replay, start session JSON.
    // quiet: skip ARMED last_run/console spam (matrix prep uses this).
    bool prepare(CaseSpec const& spec, std::string& report, bool quiet = false);
    // Load corpus level.gmd into PlayLayer (opt-in; default runner never calls this).
    bool openPreparedLevel(std::string& report);
    // Default: prepare only (menu stays). Pass openLevel=true or EB_REAL_ASSIST_OPEN_LEVEL=1 to enter.
    bool runCase(CaseSpec const& spec, std::string& report, bool openLevel = false);
    static bool wantOpenLevel();

    void onLevelInit(PlayLayer* layer);
    void onAttemptEnd(PlayLayer* layer, bool completed, float endPercent);
    void finish(std::string& report);

    // Dev / Lab button.
    bool runDevAction(std::string& report);
    // After mod load: arm auto; fires once from MenuLayer::init (survives loading scenes).
    static void maybeQueueAutoFromEnv();
    // Called from EbMenuLayer::init — runs armed auto once on the main menu.
    static void onMenuLayerReady();

    static bool selfCheck(std::string& report);

private:
    EbRealAssistRunner() = default;

    bool resolvePaths(CaseSpec const& spec, std::filesystem::path& levelPath,
                      std::filesystem::path& replayPath, std::string& err) const;
    bool applyProtocol(CaseSpec const& spec, std::string& err);
    bool restoreProtocol(std::string& err);
    bool importReplayFile(std::filesystem::path const& path, std::string& err);
    bool writeSession(std::string& err) const;
    bool writeLastRun(std::string const& text, std::string& err) const;
    std::string summaryBlock(bool prepOk) const;
    static std::string envOrEmpty(char const* name);
    static bool envTruthy(char const* name);

    static bool s_autoArmed;
    static bool s_autoFired;

    bool m_active = false;
    bool m_levelOpened = false;
    bool m_protocolApplied = false;
    bool m_skipNextResetRecord = false;
    // Ignore PlayLayer setup resets until the first post-init reset is consumed.
    bool m_recordingAttempts = false;
    bool m_skipSetupReset = false;
    bool m_prevAutoTune = false;
    bool m_prevHybridFrozen = false;
    CaseSpec m_case{};
    std::vector<AttemptRow> m_attempts;
    std::filesystem::path m_sessionPath;
    std::string m_lastError;
    int m_baselineArms = 0;
    int m_baselineDaf = 0;
    int m_baselineOpp = 0;
};

#endif
