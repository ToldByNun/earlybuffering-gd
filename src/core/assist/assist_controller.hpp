#ifndef ASSIST_CONTROLLER_HPP
#define ASSIST_CONTROLLER_HPP

#include "../../config/stage_config.hpp"
#include "../../dev/capture/dev_clock_contract.hpp"
#include "../bake/bake_verifier.hpp"
#include "../diagnostics/decision_log.hpp"
#include "../sim/shadow_simulator.hpp"
#include "planning/orb_align.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>

enum class BufferState {
    Idle,
    Buffered,    // action swallowed; waiting until fireTick, then we inject it
    FiredHolding // legacy — cleared if seen
};

class EbBufferedInput {
public:
    bool fixedFireTick = false;
    BufferState state = BufferState::Idle;
    bool down = true;
    bool player1 = true;
    int pressTick = 0;
    int fireTick = 0;
    int limit = 0;
    int expectedJumpTick = 0;
    // Plan delay at arm time (before jumpTick alignment). SaveFeedback uses
    // actual attemptTick - pressTick at inject, not this field.
    int planDelay = 0;
    bool continuous = false;
    // Early UP while Buffered: true, buffer stays Buffered — inject still due at fireTick.
    // (Not a cancel. At fire: queued hold via releaseAfter/requireHold, else ASAP release.)
    bool releaseSeen = false;
    int releaseTick = 0;
    bool jumpSeen = false;
    bool injected = false;
    int rechecks = 0;
    float decisionX = 0.f;
    GameModeKind mode = GameModeKind::Cube;
    bool requireOrb = false;
    bool requireHold = false;
    int dMin = -1;
    int dMax = -1;
    // Safe hold window after inject (ticks after fire). -1 = unset.
    int releaseAfter = -1;
    int releaseEarliest = -1;
    int releaseLatest = -1;
    int planId = 0;
    // Dev Lab: pressId of the armed cycle (0 = not tracing). Survives until fire.
    int64_t devPressId = 0;
    bool awaitingOutcome = false;
    int outcomeUntilTick = 0;
    // Soft hold: foresight/opportunity but no hard Delay yet — replan until
    // Delay confirms, opportunity clears, or pressTick+limit deadline.
    bool holdUntilSafe = false;
    // Armed from BakeMap template (Hybrid/Replay/Brute) — overlay ownership.
    bool fromBake = false;
    // HUS replan cadence (C11): skip full plan() unless interval / ΔX / foresight edge.
    int husLastReplanTick = -1;
    float husLastReplanX = 0.f;
    bool husLastForesight = false;

private:
};

class EbAssistController {
public:
    static EbAssistController& get();

    void resetAttempt(PlayLayer* playLayer = nullptr);
    void onLevelInit(PlayLayer* playLayer);
    void onLevelQuit();

    void onPhysicsTick(PlayLayer* playLayer, float deltaTime);
    void beginRealUpdate();
    void endRealUpdate();
    void onSubStep(PlayerObject* player, float deltaTime);

    bool onHandleButton(GJBaseGameLayer* layer, bool down, int button, bool player1);
    bool allowPushButton(PlayerObject* player, PlayerButton button);

    void noteRealJump(PlayerObject* player);
    void onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object);
    void onCheckpointLoad();
    void onLevelComplete(PlayLayer* playLayer);
    bool assistTouchedRun() const {
        return m_assistTouchedRun;
    }

    void resetLevelProfile();

    // Bake map: capture at attempt start; run offline bake from pause UI.
    int startSnapshotSlot() const {
        return m_startSnapshotSlot;
    }
    geode::Result<int> runBakeMap(PlayLayer* layer, bool bruteForce = false);
    // Ensure start snapshot exists (call before bake).
    geode::Result<> ensureStartSnapshot(PlayLayer* layer);

    bool isInjecting() const;
    EbBufferedInput const& buffer() const;
    int attemptTick() const;
    float lastDt() const;
    // Physics step quantum after timeWarp strip (plan / remap).
    float simDt() const {
        return this->m_simDt;
    }
    // Physics TPS = 1/m_simDt (timeWarp stripped). Authors bake/trigger keys.
    double estimatedTps() const;
    // Raw update TPS = 1/m_lastDt (debug/overlay only — never author keys).
    double wallTps() const;
    int currentCps() const;
    bool cpsLimited() const;

    void updateOverlay();
    std::string overlayText() const;

    // effectTick attribution (exposed for PlanGold / selfCheck).
    // Continuous modes (wave/ship/swing/ufo/spider): always attemptTick —
    // never attemptTick+1; CBF remainder lives only in firstDt.
    // Discrete modes: mid-step → attemptTick; between frames → attemptTick+1.
    static bool continuousModePlayer(PlayerObject* player);
    static bool continuousModeKind(GameModeKind kind);
    // Purple viz / release assist mode gate: wave/ship/swing/robot (not ufo/spider).
    static bool releaseAssistModeKind(GameModeKind kind);
    static bool releaseAssistModePlayer(PlayerObject* player);
    // Full release gate: mode above, or dashing / hold-pending / bake holdAssist.
    bool shouldPlanRelease(PlayLayer* playLayer, PlayerObject* player, bool player1,
                           int effectTick) const;
    static bool releaseGateSelfCheck(std::string& report);
    static int resolveEffectTick(int attemptTick, bool midStep, bool continuous);
    static bool effectTickSelfCheck(std::string& report);

    // Fire tick vs sim jumpTick. Non-orb: if jump lands after press in sim,
    // inject on jumpTick (avoids ±1-early press). Orb: keep activation delay.
    // Clamps to [effectTick+delay floor via caller window] — see armFireTicks.
    static int resolveFireTick(int effectTick, int delay, int jumpTick, bool requireOrb);
    static bool shouldFireBuffered(int attemptTick, int fireTick, int pressTick, int planDelay,
                                   int limit);
    static bool fireTickSelfCheck(std::string& report);

    // Queued hold after inject when user already released (releaseSeen).
    // Continuous: earliest-safe only (never blind +limit). Plain tap / unset → 0 (ASAP).
    static int resolveQueuedHoldTicks(bool requireHold, bool continuous, int releaseAfter,
                                      int releaseEarliest, int releaseLatest, int extendCap);
    static bool queuedHoldSelfCheck(std::string& report);

    // Soft Hold-Until-Safe (press or release): PassBudget / hopeless / foresight —
    // never past limit. Commit 13: only when trustOk (sonst Pass).
    // Release: swallow UP, keep Jump held, inject release when Delay confirms.
    static bool shouldArmHoldUntilSafe(EbSimPlan const& plan, bool trustOk = true);
    static int holdUntilSafeEtaTicks(EbSimPlan const& plan, int limit);
    // Full plan() only every N ticks, on ΔX, foresight edge, first call, or near deadline.
    static constexpr int kHusReplanEveryTicks = 3;
    static constexpr float kHusReplanDeltaX = 8.f;
    static bool shouldReplanHoldUntilSafe(int attemptTick, int lastReplanTick, float x,
                                          float lastReplanX, bool foresightNow, bool foresightLast,
                                          int remainingTicks);
    static bool holdUntilSafeSelfCheck(std::string& report);

    // Recheck slide (Commit 13): never earlier; late ≤+1 (OrbAlign::kRecheckMaxSlide).
    // requireOrb kept for API; slide cap is shared. Returns -1 = reject.
    static constexpr int kRecheckMaxSlide = EbOrbAlign::kRecheckMaxSlide;
    static int acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire, int absMax,
                                  int robustK, int deadline, bool requireOrb, bool planOk);
    static int estimateRobustK(PlayerObject* player, float dtVariance);
    static bool recheckSlideSelfCheck(std::string& report);

private:
    friend class EbLevelLifecycle;

    friend class EbServiceStore;

    friend class EbAssistReleasePlanner;

    friend class EbAssistPressPlanner;

    friend class EbAssistBakeReuse;

    friend class EbAssistOutcomes;

    friend class EbAssistOverlay;

    friend class EbAssistInputRuntime;

    friend class EbAssistBufferRuntime;

    friend class EbAssistHoldRuntime;

    friend class EbHoldUntilSafePolicy;

    friend class EbAssistInputGate;

    friend class EbAssistTimingRules;

    friend class EbAssistLifecycle;

    EbAssistController() = default;

    int activeLimit(PlayerObject* player) const;
    bool shouldAssistPlayer(PlayerObject* player) const;
    void clearBuffer();
    void inject(GJBaseGameLayer* layer, bool down);
    void syncButtonState(PlayLayer* playLayer);
    bool planPress(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick,
                   float firstDt, EbDevClockContract::Stamp const& clock);
    bool planRelease(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick,
                     float firstDt, EbDevClockContract::Stamp const& clock);
    // Returns true if bake handled the input (caller should return that bool).
    // Sets handled=false if bake misses / verify fails and Hybrid should fall through to live.
    // sharedBudget: Hybrid press envelope (verify spends first; Live uses remainder).
    // verifyAttempted: set when BakeVerifier::verify ran (Hybrid Live must not
    // re-force assist via nearBake / prefer-band from the rejected window).
    // reuse: copy findNear window + verify threatZone for Live (no bake ptrs).
    bool tryBakeAssist(PlayLayer* playLayer, PlayerObject* player, bool player1, bool down,
                       int effectTick, float firstDt, bool& handled,
                       EbPressSimBudget* sharedBudget = nullptr, bool* verifyAttempted = nullptr,
                       EbHybridPressReuse* reuse = nullptr);
    // allowRecheck=false for CPS / mode-change force-fire (inject now).
    void fireBuffered(PlayLayer* playLayer, PlayerObject* player, bool allowRecheck = true);
    void flushPendingRelease(PlayLayer* playLayer);
    bool userHolding(bool player1) const {
        return m_userHolding[player1 ? 0 : 1];
    }
    void noteUserClick();
    void pruneClickTimes();
    void beginOutcome(EbSimPlan const& plan, int effectTick);
    void pollOutcome(PlayLayer* playLayer);
    void logPlan(EbSimPlan const& plan, char const* kind);
    void noteVerdict(AssistReason reason, char const* where);
    // Arm soft hold buffer (swallows press or release). Returns false for onHandleButton.
    bool armHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player, bool player1, int effectTick,
                          int limit, EbSimPlan const& plan, bool down = true);
    // Cadenced replan while holdUntilSafe buffer is armed (not every tick).
    void tickHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player);

    void recordDecision(PlayLayer* playLayer, PlayerObject* player, bool down, EbSimPlan const& plan,
                        int effectTick, int appliedDelay, int limit);
    void tickLearning(PlayLayer* playLayer, PlayerObject* player);
    void runPostmortem(PlayLayer* playLayer, PlayerObject* player, GameObject* object,
                       EbDecisionEntry& dec);

    EbBufferedInput m_buffer{};
    EbDecisionLog m_log{};
    bool m_injecting = false;
    int m_attemptTick = 0;
    float m_lastDt = 1.f / 240.f;
    float m_simDt = 1.f / 240.f;
    float m_dtEma = 1.f / 240.f;
    float m_dtVarEma = 0.f; // relative |dt−ema|/ema for auto robustK
    bool m_inRealUpdate = false;
    float m_subDtSeen = 0.f;
    bool m_userHolding[2] = {false, false};
    bool m_modeShip = false;
    bool m_modeBall = false;
    bool m_modeBird = false;
    bool m_modeDart = false;
    bool m_modeRobot = false;
    bool m_modeSpider = false;
    bool m_modeSwing = false;
    bool m_modeKnown = false;
    bool m_pendingRelease = false;
    bool m_pendingReleaseP1 = true;
    bool m_pendingReleaseHold = false;
    bool m_pendingReleaseContinuous = false;
    int m_pendingReleaseArmTick = 0;     // attemptTick when release was armed
    int m_pendingReleaseUserUpTick = -1; // -1 = user still holding; else let-go tick
    int m_pendingReleaseMaxExtend = 0;   // max ticks to keep Jump down AFTER user-up
    int m_pendingReleaseTick = 0;        // absolute deadline (= userUp + maxExtend)
    int64_t m_pendingReleaseDevPressId = 0; // Dev Lab hold timeline (AP7)
    float m_lastPlayerX = 0.f;
    GameModeKind m_lastMode = GameModeKind::Cube;
    int m_startSnapshotSlot = -1;
    bool m_startSnapshotTaken = false;
    std::string m_lastBakeHint;
    std::string m_overlayText = "EarlyBuffer: OFF";
    std::deque<std::chrono::steady_clock::time_point> m_clickTimes;
    bool m_cpsLocked = false;
    int m_outcomePlanId = 0;
    bool m_outcomePending = false;
    int m_outcomeUntil = 0;
    bool m_outcomePlayer1 = true;
    bool m_outcomeExpectOrb = false;
    bool m_outcomePlanDelay = false; // true if plan was Delay (not Pass)
    float m_outcomeStartX = 0.f;
    bool m_untrustPassHint = false; // overlay: forced Pass under simUntrusted
    AssistReason m_lastVerdict = AssistReason::None;
    bool m_assistTouchedRun = false;
    int m_savesAttempt = 0;
    int m_savesLevel = 0;
    // Commit 2 real metrics: death after a fired Delay inject (cleared on reset).
    bool m_dafWatch = false;
};

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

class EbAssistControllerImplementation {
public:
    static int roundBias(float bias);
    static void fillPlanRobustLatency(EbPlanOptions& opts, PlayerObject* player, float dtVariance,
                                      bool spamDense = false);
    static void fillBakePreferBandFromReuse(EbPlanOptions& opts, EbHybridPressReuse const& reuse,
                                            int effectTick, int limit, float simDt);
    static void fillBakePreferBand(EbPlanOptions& opts, PlayerObject* player, bool player1, bool down,
                                   int effectTick, int limit, int attemptTick, float simDt,
                                   EbHybridPressReuse const* reuse = nullptr);
    static bool nearBakeWindowHint(PlayerObject* player, bool player1, bool down, int attemptTick,
                                   float simDt, EbHybridPressReuse const* reuse = nullptr);
    static bool husBlockedBeforeBakeNominal(EbHybridPressReuse const& reuse, int effectTick,
                                            float simDt, int& outNomTick);
    static bool nearOrbOrPadHint(PlayLayer* playLayer, PlayerObject* player, int limit,
                                 float simDt);

private:
    friend class EbAssistReleasePlanner;

    friend class EbAssistPressPlanner;

    friend class EbAssistOverlay;

    friend class EbAssistInputRuntime;

    friend class EbAssistBufferRuntime;

    friend class EbAssistHoldRuntime;

    friend class EbBufferedInput;
    friend class EbAssistController;

    inline static constexpr int JUMP_BUTTON = 1;

    EbAssistControllerImplementation() = delete;
};

#endif
