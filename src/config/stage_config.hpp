#ifndef STAGE_CONFIG_HPP
#define STAGE_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

class PlayerObject;

class StageInfo {
public:
    bool enabled = true;
    int ticksAll = 4;
    int ticksWave = 4;

private:
};

class ModeParams {
public:
    bool enabled = true;
    bool overwrite = false;
    int ticks = 2;
    int cap = 0; // 0 = auto
    int slack = 2;
    // betterCand: score = endX − latencyWeight · delay (Wave high, Cube low).
    float latencyWeight = 0.25f;

private:
};

class StageConfig {
public:
    enum class GameModeKind { Cube = 0, Ship, Ball, Ufo, Wave, Robot, Spider, Swing, Count };

    enum class AssistMode { Live = 0, Replay, Hybrid, BruteForce };

    // Live V2 sim backend (WU8). V1 effective engine is always ShadowPlayer.
    enum class AssistEngineKind { ShadowPlayer = 0, Trajectory };

    static constexpr int MODE_COUNT = static_cast<int>(GameModeKind::Count);

    static void refreshCache();

    static StageInfo fromSettings();
    static void toggleDebugLogging();
    // Toggle smart / per-level learning (bias, trust, capMul). Window ticks unchanged.
    static void toggleAutoTune();
    static void cycleAssistMode();
    static void setAssistMode(AssistMode mode);
    static void toggleShowTrajectory();

    static bool isApplying();
    // Bulk setting writes (ConfigBundle import) — suppresses overlay listeners.
    static void beginApplying();
    static void endApplying();

    // Setting groups mirror mod.json / tabs: Main (Window) → Scope → Assist →
    // Bake → Modes → Visual → Keys → Dev.

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
    static bool showBakePath();  // green press + purple release BakeMap windows
    static bool showPauseBake(); // pause-screen status lines
    // Pause top-right button → Geode openSettingsPopup (later: hub).
    static bool showPauseHub();
    // Independent save-feedback toggles (sound / icon flash / frame counter).
    static bool saveFeedbackSound();
    static bool saveFeedbackIcon(); // Save Feedback Color (icon flash)
    static bool saveFeedbackCounter();
    class ColorRgb {
    public:
        uint8_t r = 124;
        uint8_t g = 255;
        uint8_t b = 74;

    private:
    };
    static ColorRgb saveFeedbackColor(); // picker used when saveFeedbackIcon()

    // --- Assist Scope (practice / startpos / percent / off-ranges) ---
    static bool assistOnlyPractice();
    static bool assistStartposOnly();
    static int assistPercentMin();
    static int assistPercentMax();
    class PercentRange {
    public:
        float from = 0.f;
        float to = 0.f;

    private:
    };
    static constexpr int ASSIST_OFF_SLOTS = 6;
    // Ranges where assist is forced off (inclusive). From float settings assist-off-N-from/to (-1 =
    // unused).
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
    // Assist pick inside the tick window: v1 (default, ex-Legacy) vs v2 (ex-New).
    // assistPickLegacy() == v1 (kept name until Live choose extract).
    static bool assistPickLegacy();
    static bool assistPickV1();
    static char const* assistPickName(); // "v1" / "v2"
    // Map store/export aliases: v1|Legacy → v1, v2|New → v2. Empty if unknown.
    static char const* canonicalizeAssistPick(std::string const& raw);
    // Raw assist-engine setting (ShadowPlayer / Trajectory).
    static AssistEngineKind assistEngine();
    // V1 → always ShadowPlayer; V2 → honors assist-engine.
    static AssistEngineKind effectiveAssistEngine();
    static char const* assistEngineName(AssistEngineKind engine);
    static char const* assistEngineName(); // effective
    static int simBudgetUs();
    static int bakeRadius();
    static int cpsLimit();           // 0 = off; assist pauses while CPS >= this
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

    // --- Per-mode ---
    static GameModeKind kindOf(PlayerObject* player);
    static char const* kindName(GameModeKind kind);
    static ModeParams paramsFor(GameModeKind kind);
    static ModeParams paramsFor(PlayerObject* player);

    // Overlay / fingerprint: "OFF" or "NA/NW" from Main Window values.
    static std::string stageLabel(StageInfo const& info);

private:
    friend class StageSettingsCache;
};

using GameModeKind = StageConfig::GameModeKind;
using AssistMode = StageConfig::AssistMode;
using AssistEngineKind = StageConfig::AssistEngineKind;

#include "../core/assist/assist_clock.hpp"
#include "../core/platform/platform_limits.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

class ModeSettingKeys {
public:
    char const* enable;
    char const* overwrite;
    char const* ticks;
    char const* cap;
    char const* slack;
    char const* latency;
    char const* name;

private:
};
class StageConfigImplementation {
public:
    static float defaultLatencyWeight(int modeIndex);
    static int clampInt(int value, int lo, int hi);
    static int readInt(geode::prelude::Mod* mod, char const* id, int fallback);
    static bool readBool(geode::prelude::Mod* mod, char const* id, bool fallback);
    static float clampPct(float value);
    static float readFloat(geode::prelude::Mod* mod, char const* id, float fallback);
    static float round2(float value);
    static void loadAssistOffSlots(geode::prelude::Mod* mod,
                                   std::vector<StageConfig::PercentRange>& out);
    static void ensureCache();

private:
    friend class StageSettingsCache;

    friend class StageInfo;
    friend class ModeParams;
    friend class StageConfig;
    friend class ModeSettingKeys;

    inline static constexpr ModeSettingKeys MODE_IDS[] = {
        {"cube-enable", "cube-overwrite", "cube-ticks", "cube-cap", "cube-slack", "cube-latency",
         "cube"},
        {"ship-enable", "ship-overwrite", "ship-ticks", "ship-cap", "ship-slack", "ship-latency",
         "ship"},
        {"ball-enable", "ball-overwrite", "ball-ticks", "ball-cap", "ball-slack", "ball-latency",
         "ball"},
        {"ufo-enable", "ufo-overwrite", "ufo-ticks", "ufo-cap", "ufo-slack", "ufo-latency", "ufo"},
        {"wave-enable", "wave-overwrite", "wave-ticks", "wave-cap", "wave-slack", "wave-latency",
         "wave"},
        {"robot-enable", "robot-overwrite", "robot-ticks", "robot-cap", "robot-slack",
         "robot-latency", "robot"},
        {"spider-enable", "spider-overwrite", "spider-ticks", "spider-cap", "spider-slack",
         "spider-latency", "spider"},
        {"swing-enable", "swing-overwrite", "swing-ticks", "swing-cap", "swing-slack",
         "swing-latency", "swing"},
    };
    static StageInfo s_cachedStage;
    static std::array<ModeParams, StageConfig::MODE_COUNT> s_cachedModes;
    static bool s_cachedDebug;
    static bool s_cachedPerfTrace;
    static bool s_cachedLagProbe;
    static bool s_cachedSafeMode;
    static bool s_cachedShowOverlay;
    static bool s_cachedShowTrajectory;
    static bool s_cachedShowBakePath;
    static bool s_cachedShowPauseBake;
    static bool s_cachedShowPauseHub;
    static bool s_cachedAssistPress;
    static bool s_cachedAssistRelease;
    static bool s_cachedAssistOrbs;
    static bool s_cachedAutoTune;
    static bool s_cachedSelfBake;
    static bool s_cachedSelfBakeNormal;
    static bool s_cachedSelfBakeRefine;
    static bool s_cachedTriggerBake;
    static bool s_cachedTriggerBakeSkipDynamic;
    static AssistMode s_cachedAssistMode;
    static bool s_cachedAssistPickLegacy;
    static AssistEngineKind s_cachedAssistEngine;
    static int s_cachedSimBudgetUs;
    static int s_cachedBakeRadius;
    static int s_cachedCpsLimit;
    static int s_cachedMinClickIntervalMs;
    static int s_cachedSmartDepth;
    static int s_cachedSmartCombos;
    static int s_cachedSmartHorizon;
    static int s_cachedRobustK;
    static bool s_cachedRobustJitter;
    static std::string s_cachedAssistVerify;
    static int s_cachedTickBudget;
    static bool s_cachedAssistOnlyPractice;
    static bool s_cachedAssistStartposOnly;
    static int s_cachedAssistPercentMin;
    static int s_cachedAssistPercentMax;
    static std::vector<StageConfig::PercentRange> s_cachedAssistDisableRanges;
    static bool s_cachedSaveFeedbackSound;
    static bool s_cachedSaveFeedbackColor;
    static bool s_cachedSaveFeedbackCounter;
    static StageConfig::ColorRgb s_cachedSaveIconColor;
    static int s_cachedSlack;
    static int s_cachedLookAhead;
    static bool s_cachedWindowUnitFrames;
    static bool s_cacheInitialized;
    static bool s_applying;

    StageConfigImplementation() = delete;
};

#endif
