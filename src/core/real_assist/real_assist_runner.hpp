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

    static char const* protocolName(Protocol protocol);
    static bool parseProtocol(std::string const& text, Protocol& out);
    static bool parseMode(std::string const& text, EbStageConfig::AssistMode& out);
    static bool parseCaseString(std::string const& text, CaseSpec& out, std::string& err);

    // Smoke matrix: 4 ids × {Live,Hybrid} × {on-time, pulse-shift-early-2} × Frozen.
    static std::vector<CaseSpec> smokeMatrix();

    bool active() const;
    CaseSpec const& current() const;

    // Resolve files, apply mode/protocol, import shifted replay, start session JSON.
    bool prepare(CaseSpec const& spec, std::string& report);
    // Load corpus level.gmd into PlayLayer (GDShare plist via DS_Dictionary).
    bool openPreparedLevel(std::string& report);
    // prepare + open in one shot.
    bool runCase(CaseSpec const& spec, std::string& report);

    void onLevelInit(PlayLayer* layer);
    void onAttemptEnd(PlayLayer* layer, bool completed, float endPercent);
    void finish(std::string& report);

    // Dev / Lab button.
    bool runDevAction(std::string& report);
    // After mod load: wait for MenuLayer, then auto case or matrix.
    static void maybeQueueAutoFromEnv();

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
    static void queueAutoWhenMenuReady();

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
