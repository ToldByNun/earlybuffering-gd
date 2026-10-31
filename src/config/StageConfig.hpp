#ifndef EARLYBUFFER_CONFIG_STAGECONFIG_HPP
#define EARLYBUFFER_CONFIG_STAGECONFIG_HPP

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
    // betterCand: score = endX − latencyWeight · delay (Wave high, Cube low).
    float latencyWeight = 0.25f;
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
    // Bulk setting writes (ConfigBundle import) — suppresses preset / overlay listeners.
    static void beginApplying();
    static void endApplying();
    // Remember the last preset we applied so load-time listener spam can't wipe custom ticks.
    static void notePresetSynced(std::string const& name);
    static std::string lastSyncedPreset();

    // Setting groups mirror mod.json / tabs: Main → Scope → Assist (Window /
    // Algorithm / Smart / Learning / Trigger World) → Bake → Modes → Visual →
    // Keys → Dev.

    // --- General / UI ---
    static bool isEnabled();
    static bool debugLogging();
    // Section timings + spike dumps to Geode log and save-dir earlybuffer-perf.log.
    static bool perfTrace();
    // Death/reset/checkpoint episode timings → earlybuffer-lagprobe.log.
    static bool lagProbe();
    static bool safeMode();
    // Live-reads safe-mode + enabled. false when Safe Mode is OFF (always allow progress).
    // When Safe Mode is ON: block if assist is enabled or this attempt was assist-touched.
    static bool blocksProgressSaves(bool assistTouchedRun);
    static bool showOverlay();
    static bool showTrajectory();
    static bool showPauseBake(); // pause-screen status lines
    // Pause top-right button → Geode openSettingsPopup (later: hub).
    static bool showPauseHub();
    // Independent save-feedback toggles (sound / icon flash / frame counter).
    static bool saveFeedbackSound();
    static bool saveFeedbackIcon(); // Save Feedback Color (icon flash)
    static bool saveFeedbackCounter();
    struct ColorRgb {
        uint8_t r = 124;
        uint8_t g = 255;
        uint8_t b = 74;
    };
    static ColorRgb saveFeedbackColor(); // picker used when saveFeedbackIcon()

    // --- Assist Scope (practice / startpos / percent / off-ranges) ---
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

    // --- Default Window ---
    // When true, ticks-all / ticks-wave / per-mode ticks are stored as *display frames*
    // (60fps) and converted to physics ticks via AssistClock (1 frame ≈ 4 ticks @240).
    static bool windowUnitFrames();
    static int windowValueToTicks(int value);
    static int ticksToWindowValue(int ticks);
    static int ticksAll();
    static int ticksWave();

    // --- Algorithm ---
    static bool assistPress();
    static bool assistRelease();
    static bool assistOrbs(); // orb contact-window ranking (Live/Hybrid)
    static AssistMode assistMode();
    static char const* assistModeName(AssistMode mode);
    static char const* assistModeHint(AssistMode mode);
    // Assist pick inside the tick window: Legacy (default, 1.2.0 feel) vs New.
    static bool assistPickLegacy();
    static char const* assistPickName();
    static int simBudgetUs();
    static int bakeRadius();
    static int cpsLimit(); // 0 = off; assist pauses while CPS >= this
    static int minClickIntervalMs(); // 0 = off; skip assist (click still passes)
    static int tickBudgetPerPress();
    // Hold-Until-Safe replan caps (derived from sim-budget-us / tick-budget).
    // Defaults: budgetUs = max(200, sim/2), tickBudget = max(200, tick/4).
    static int husReplanBudgetUs();
    static int husReplanTickBudget();
    static int deathSlack();
    static int lookAheadCap();

    // --- Smart Prediction (Live / Hybrid) ---
    // Live/Hybrid multi-jump: 1 = single-click only
    static int smartDepth();
    static int smartCombos();
    static int smartHorizon(); // 0 = auto
    // ±k robust window: 0 = auto from mode / speed / dt variance; else 1–3.
    static int robustK();
    // Re-check top cand with firstDt ±3% before committing Delay.
    static bool robustJitter();

    // --- Learning ---
    static bool autoTune();
    // Self-bake: practice default; optional normal-mode; budgeted window refine.
    static bool selfBake();
    static bool selfBakeNormal();
    static bool selfBakeRefine();
    // Trigger World: static mover cache for shadow sim; skip dynamic on weak PCs.
    static bool triggerBake();
    static bool triggerBakeSkipDynamic();
    // "off" | "ab" — A/B verify when legacy/new disagree (optional)
    static char const* assistVerifyMode();
    static bool assistVerifyAb();

    // --- Internal ---
    static int stageIndex();

    // --- Per-mode ---
    static GameModeKind kindOf(PlayerObject* player);
    static char const* kindName(GameModeKind kind);
    static ModeParams paramsFor(GameModeKind kind);
    static ModeParams paramsFor(PlayerObject* player);

    static std::string stageLabel(StageInfo const& info);
};

using GameModeKind = StageConfig::GameModeKind;
using AssistMode = StageConfig::AssistMode;

#endif // EARLYBUFFER_CONFIG_STAGECONFIG_HPP
