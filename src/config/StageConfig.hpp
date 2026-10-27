#ifndef FRIRECRACKER_CONFIG_STAGECONFIG_HPP
#define FRIRECRACKER_CONFIG_STAGECONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

class PlayerObject;

struct StageInfo {
    int index = 0;       // 0..4 (4 = OFF)
    bool enabled = true;
    int ticksAll = 2;
    int ticksWave = 2;
};

struct ModeParams {
    bool enabled = true;
    bool overwrite = false;
    int ticks = 2;
    int cap = 0;     // 0 = auto
    int slack = 2;
};

class StageConfig {
public:
    enum class GameModeKind {
        Cube = 0,
        Ship,
        Ball,
        Ufo,
        Wave,
        Robot,
        Spider,
        Swing,
        Count
    };

    enum class AssistMode {
        Live = 0,
        Replay,
        Hybrid,
        BruteForce
    };

    static constexpr int STAGE_COUNT = 5;
    static constexpr int MODE_COUNT = static_cast<int>(GameModeKind::Count);

    static char const* presetName(int index);
    static int presetIndexByName(std::string const& name);

    static void refreshCache();

    static StageInfo fromSettings();
    static StageInfo preset(int index);
    // Fills default ticks / slack / look-ahead. Does NOT touch per-mode overwrites.
    // Only call from F6 cycle or an explicit Preset dropdown change — never on load.
    static void applyPreset(int index);
    static void applyPresetByName(std::string const& name);
    static StageInfo cycleNext();
    static void toggleDebugLogging();
    // Toggle smart / per-level learning (bias, trust, capMul). Window ticks unchanged.
    static void toggleAutoTune();
    static void cycleAssistMode();
    static void setAssistMode(AssistMode mode);
    static void toggleShowTrajectory();

    static bool isApplying();
    // Remember the last preset we applied so load-time listener spam can't wipe custom ticks.
    static void notePresetSynced(std::string const& name);
    static std::string lastSyncedPreset();

    static bool isEnabled();
    static bool debugLogging();
    static bool safeMode();
    static bool showOverlay();
    static bool showTrajectory();
    static bool showPauseBake(); // pause-screen bake / mode buttons
    static bool assistPress();
    static bool assistRelease();
    static bool assistOrbs(); // orb contact-window ranking (Live/Hybrid)
    static bool swallowHopeless();
    static bool autoTune();
    static AssistMode assistMode();
    static char const* assistModeName(AssistMode mode);
    static char const* assistModeHint(AssistMode mode);
    static int simBudgetUs();
    static int bakeRadius();
    static int cpsLimit(); // 0 = off; assist pauses while CPS >= this
    static int minClickIntervalMs(); // 0 = off; skip assist (click still passes)
    // Live/Hybrid multi-jump: 1 = single-click only
    static int smartDepth();
    static int smartCombos();
    static int smartHorizon(); // 0 = auto
    // "off" | "ab" — A/B verify when legacy/new disagree (optional)
    static char const* assistVerifyMode();
    static bool assistVerifyAb();
    static int tickBudgetPerPress();
    static int ticksAll();
    static int ticksWave();
    static int deathSlack();
    static int lookAheadCap();
    static int stageIndex();

    static GameModeKind kindOf(PlayerObject* player);
    static char const* kindName(GameModeKind kind);
    static ModeParams paramsFor(GameModeKind kind);
    static ModeParams paramsFor(PlayerObject* player);

    static bool assistOnlyPractice();
    static bool assistStartposOnly();
    static int assistPercentMin();
    static int assistPercentMax();
    struct PercentRange {
        float from = 0.f;
        float to = 0.f;
    };
    static constexpr int ASSIST_OFF_SLOTS = 6;
    // Ranges where assist is forced off (inclusive). From float settings assist-off-N-from/to (-1 = unused).
    static std::vector<PercentRange> const& assistDisableRanges();
    static bool isInAssistDisableRange(float percent);
    static bool showPauseAssist();
    static char const* saveFeedbackMode();

    static std::string stageLabel(StageInfo const& info);
};

using GameModeKind = StageConfig::GameModeKind;
using AssistMode = StageConfig::AssistMode;

#endif // FRIRECRACKER_CONFIG_STAGECONFIG_HPP
