#ifndef SHADOW_SIMULATOR_HPP
#define SHADOW_SIMULATOR_HPP

class EbSimulationCandidate;

#include "../../config/stage_config.hpp"
#include "../../dev/capture/dev_clock_contract.hpp"
#include "../../dev/capture/dev_live_witness.hpp"
#include "../../dev/evaluation/dev_eval_restore.hpp"
#include "../../dev/evaluation/dev_reject_ledger.hpp"
#include "../assist/contracts/assist_verdict.hpp"
#include "../replay/replay_import.hpp"
#include "planning/simulation_plan.hpp"
#include "shadow_run_result.hpp"
#include <Geode/Geode.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Lexicographic planner: threat-zone clear, orb activation windows, adaptive ±k
// robustness, optional firstDt jitter, latency-weighted endX. Never blocks (Pass).
class EbShadowSimulator {
public:
    using RunResult = EbShadowRunResult;

    static constexpr int SNAPSHOT_SLOTS = 16;
    // Slot 0 is immortal attempt-start (bake). Plan/traj ring uses 1..13.
    // Slots 14–15 are reserved for GhostParityProbe anchors (not rotated by plan).
    static constexpr int START_SNAPSHOT_SLOT = 0;
    static constexpr int SNAPSHOT_RING_BEGIN = 1;
    static constexpr int SNAPSHOT_RING_END = 14; // exclusive upper bound for plan ring
    static constexpr int PARITY_SLOT_A = 14;
    static constexpr int PARITY_SLOT_B = 15;
    // Permanent eval ghosts (Commit 9) — not the 16 live snapshot slots.
    static constexpr int DEV_EVAL_SLOTS = 2;
    static constexpr float kClusterGapX = 50.f;
    static constexpr int kWindowMarginK = 1;
    static constexpr int kDefaultTickBudget = 4000;
    // Wave zigzag: hazard AABB within this ahead-X (+ auto from limit) → Pass.
    static constexpr float kHazardProximityXMin = 70.f;
    static constexpr float kHazardProximityXMax = 140.f;
    // Delays that do not improve surviveBin / endX beyond this → Pass (wave).
    static constexpr float kFutileEndXGain = 8.f;

    static EbShadowSimulator& get();

    void attach(PlayLayer* layer);
    // layerDying=true: only drop pointers (parent tree already tearing down).
    // Prefer detach(false) from onQuit after evacuateGhosts — removes ghosts
    // while PlayLayer is still alive (avoids ~PlayerObject release AVs).
    void detach(bool layerDying = false);

    bool isAttachedTo(GJBaseGameLayer* layer) const {
        return layer && this->m_hostLayer == layer;
    }
    bool isSimulating() const {
        return m_simulating;
    }
    // Offline bake timeline: trust current toggle pose (Include/Skip) instead of
    // UnknownOnOverlap — otherwise every toggle-group solid aborts the macro.
    bool bakeOffline() const {
        return this->m_bakeOffline;
    }
    // True after applyTriggerWorldAtTick ran for the current sim step.
    bool triggerWorldAppliedThisTick() const {
        return this->m_triggerWorldAppliedThisTick;
    }
    // Object is targeted by a player-coupled DynamicFollow (sim collision gate).
    bool isDynamicTriggerTarget(int uniqueId) const;
    // Cheap live probe wrote a pose for this id on the current sim step.
    bool liveDynamicApplied(int uniqueId) const;
    bool isShadow(PlayerObject* player) const {
        return player && (player == m_shadow || (m_evalActive && player == m_evalActive));
    }
    // Shadow + snapshot slots — never feed these into vanilla destroyPlayer.
    bool isOwnedGhost(PlayerObject* player) const;
    // Mega Hack / OpenHack "hitboxes on death" may latch on any destroyPlayer
    // (even after we swallow). While set, force-hide debug draw if P1 is alive.
    bool suppressDeathHitboxes() const {
        return m_suppressDeathHitboxes;
    }
    void noteGhostDeathHitboxSuppress() {
        m_suppressDeathHitboxes = true;
    }
    void clearDeathHitboxSuppress() {
        m_suppressDeathHitboxes = false;
    }
    // While simulating, keep real icons looking alive unless they were already dead.
    void clearContaminatedRealDeathFlags(PlayLayer* layer) {
        if (!layer || !m_simulating)
            return;
        if (layer->m_player1 && !m_savedP1Dead)
            layer->m_player1->m_isDead = false;
        if (layer->m_player2 && !m_savedP2Dead)
            layer->m_player2->m_isDead = false;
    }
    PlayerObject* shadow() const {
        return m_shadow;
    }
    bool isShadowTrail(HardStreak* trail) const {
        if (!trail)
            return false;
        if (m_shadow && trail == m_shadow->m_waveTrail)
            return true;
        if (!(m_evalActive && trail == m_evalActive->m_waveTrail)) {
            return false;
        }
        return true;
    }

    void markDead(PlayerObject* player, GameObject* object = nullptr);
    // Park ghosts off-level so stray collision cannot fail the real attempt.
    // Active sim bodies (shadow + eval) move off-level; snapshots scrub+hide only
    // so captured pose for bake/traj stays intact.
    void parkGhosts();
    // Before PlayLayer::resetLevel / checkpoint reload: pull every owned ghost out of
    // section vectors (sim checkCollisions inserts them; vanilla sortSectionVector AVs).
    void evacuateGhosts(GJBaseGameLayer* layer);
    // RAII entry/exit used by SimSessionGuard (restore + park always unwind).
    void beginSession(PlayLayer* layer);
    void endSession(PlayLayer* layer);
    // Persistent scratch for collisionCheckObjects filtering — never a stack
    // gd::vector (iOS ARM64 ABI / lifetime risk into playerTouchedRing).
    gd::vector<GameObject*>& collisionFilterScratch();
    void noteJump();
    void noteActivated(GameObject* object);
    // Overlap with UnknownOnOverlap object. Pass obj to flag toggle-target hits.
    // When unknown-hist collection is on (bake diag), records Soft/Hard bins.
    void noteUnknown(GameObject* object = nullptr);
    bool isToggleTriggerTarget(int uniqueId) const;

    // Soft/Hard unknown overlap histogram (Commit 3 bakefail forensics).
    class UnknownHistBin {
    public:
        int objectId = 0;   // m_objectID
        int objectType = 0; // GameObjectType
        int uniqueId = -1;  // last seen m_uniqueID
        bool hard = false;
        int count = 0;

    private:
    };

    void applyRingJump(PlayerObject* shadow, RingObject* ring);
    bool isApplyingRing() const {
        return m_applyingRing;
    }
    bool isOrbScanning() const {
        return m_orbScanning;
    }
    void noteOrbOverlap();
    // Absolute sim tick during runOne / bake (for OrbStack evidence).
    int simAbsTick() const {
        return this->m_simAbsTick;
    }

    static void copyPlayerState(PlayerObject* destination, PlayerObject* source);

    // Count of TW transforms captured for the current/last beginCapture (diag).
    size_t capturedTransformCount() const {
        return this->m_savedTransforms.size();
    }
    // Diag: beginCapture → count → restoreCapture (no lingering world edits).
    size_t diagCaptureTransformCount(PlayLayer* layer);

    // Rotating ring — never overwrites START_SNAPSHOT_SLOT or parity slots.
    int captureSnapshot(PlayerObject* real);
    // Attempt-start pose for Replay/Brute bake (always slot 0).
    int captureStartSnapshot(PlayerObject* real);
    // GhostParityProbe anchors: which=0 → slot 14, which=1 → slot 15.
    int captureParitySnapshot(PlayerObject* real, int which);
    PlayerObject* snapshotSlot(int slot) const;

    EbSimPlan plan(PlayLayer* layer, PlayerObject* real, bool down, int limit, float deltaTime,
                 float firstDt, EbPlanOptions const& options = {});

    EbSimPlan planSequence(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                         float deltaTime, float firstDt, EbPlanOptions const& options = {});

    // Single-candidate bake check: runOne(delay) must clear the threat zone
    // and not die before the action. Used by Hybrid bake verify (O(1) sim).
    // delay==0 reuses the d0 zone-probe runOne (no duplicate sim).
    class VerifyDelayResult {
    public:
        bool ok = false;
        bool clearsThreat = false;
        bool diedBeforeAction = false;
        bool budgetHit = false;
        bool unknown = false;
        int ticksSpent = 0; // physics ticks consumed from options.tickBudget
        int usSpent = 0;    // wall µs for this call (for shared press envelope)
        EbThreatZone threatZone{};

    private:
    };
    VerifyDelayResult verifyDelay(PlayLayer* layer, PlayerObject* real, bool down, int delay,
                                  int limit, float deltaTime, float firstDt,
                                  EbPlanOptions const& options = {});

    // --- Dev Lab eval (Commit 9): clock-bound stepSchedule, own ghosts ---
    class TrajSample {
    public:
        int tick = 0;
        float x = 0.f;
        float y = 0.f;
        float vy = 0.f;
        bool alive = true;
        bool unknown = false;

    private:
    };

    class EvalFromFreezeResult {
    public:
        bool ran = false;
        bool clockMismatch = false;
        bool aborted = false;
        bool died = false;
        int deathTick = -1;
        float endX = 0.f;
        bool unknown = false;
        bool budgetHit = false;
        int ticksRan = 0;
        int actionTick = -1; // schedule index where input fired
        uint64_t stepScheduleHash = 0;
        DevSimPathId evalSimPathId = DevSimPathId::None;
        EbDevEvalRestore restore{};
        // AP8: filled when evalDelayFromFreeze(..., captureTraj=true).
        std::vector<TrajSample> traj;

    private:
    };

    // Overlay serializable blob fields onto a ghost (eval restore path).
    static void applyPlayerBlob(PlayerObject* destination, EbDevPlayerBlob const& blob);

    // Commit 11: write dual/orbs/triggers/mover poses from witness into the
    // layer/ghosts, bookkeeping on restore. Snapshots prior world state for
    // finishEvalWorldRestore. Does not run physics.
    void applyEvalWorldRestore(PlayLayer* layer, EbDevLiveWitness const& witness,
                               EbDevEvalRestore& restore);
    // Undo layer mutations from applyEvalWorldRestore (transforms + orb flags).
    void finishEvalWorldRestore(PlayLayer* layer);

    // Counterfactual delay d on witness stamp; steps from DevClockContract::stepSchedule.
    // captureTraj (AP8): fill EvalFromFreezeResult::traj with per-tick pose samples.
    EvalFromFreezeResult evalDelayFromFreeze(PlayLayer* layer, EbDevLiveWitness const& witness,
                                             int delay, int horizonTicks, bool down,
                                             int* tickBudgetLeft = nullptr,
                                             bool captureTraj = false);

    // Replay actualFireTick (absolute) on the same stamp — not a delay search.
    EvalFromFreezeResult replayActualFire(PlayLayer* layer, EbDevLiveWitness const& witness,
                                          int actualFireTickAbs, int horizonTicks, bool down,
                                          int* tickBudgetLeft = nullptr);

    // Quit / reset: abort an in-flight eval loop.
    void abortEval();
    void clearEvalAbort() {
        this->m_evalAbort = false;
    }
    bool evalAbortRequested() const {
        return this->m_evalAbort;
    }

    // Build hazard cluster around a death X / object (public for tests/logging).
    static EbThreatZone buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj);

    // Ahead-X / jump-reach orb hint (used by bake heuristic + planner).
    static bool spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit, float deltaTime);

    // Hazard AABB within proximityX ahead of player (wave corridor spikes).
    static bool spatialHazardProximity(PlayLayer* layer, PlayerObject* real, float proximityX);

    // Pure wave anti-phase gates for plan() / selfCheck (no layer).
    // Press only — release Delay = hold longer (often the save near spikes).
    // foresightOrbPad: TravelForesight orb/pad ahead → never Pass (search/Delay).
    static bool shouldWaveHazardProximityPass(bool isWave, bool nearHazard, bool noOrbTarget,
                                              bool foresightOrbPad, bool pressDown = true);
    static bool shouldWaveFutileDelayPass(bool isWave, bool orbIntent, int bestSurviveBin,
                                          float bestEndXGain, bool foresightOrbPad,
                                          bool pressDown = true);

    // Continuous idleSafer / ascentUseful (pure; PlanGold / regressionSelfCheck).
    // Baseline idle safer than d0 press (dies later / further / not at all).
    static bool classifyIdleSafer(bool continuous, bool pressDown, bool baselineDied,
                                  int baselineDeathTick, float baselineDeathX, bool d0Died,
                                  int d0DeathTick, float d0DeathX);
    // Some delay > 0 improves on d0 — later must be Delay-worthy Alive
    // (viable + !died). Bare bin/endX gains on dead soft cands never count.
    static bool classifyAscentUseful(bool continuous, bool pressDown, bool d0Viable,
                                     int d0SurviveBin, float d0EndX, bool laterViable,
                                     int laterSurviveBin, float laterEndX, bool laterAlive);
    // Overhead / ceiling-style death relative to player Y.
    static bool deathLooksLikeAscent(float playerY, float deathY, bool upsideDown,
                                     float marginY = 20.f);

    // Prefer short release for continuous; mid-window for tap modes.
    static int pickPreferredRelease(int earliest, int latest, bool continuous);

    // Auto robustK (no setting override). Wave spam → ≥2, often 3 with speed.
    static int autoRobustK(PlayerObject* real, float dtVariance, bool spamDense = false);
    // Pure hints for selfCheck (no PlayerObject).
    static int autoRobustKHints(bool waveLike, bool isWave, float playerSpeed, float dtVariance,
                                bool spamDense);

    // Headless regression checks (threat bins / zone clear math). Returns false on fail.
    static bool regressionSelfCheck(std::string& report);

    class ReplayResult {
    public:
        bool died = false;
        int deathTick = -1;
        GameObject* deathObject = nullptr;
        bool unknown = false;
        bool activatedRing = false;
        int ringCount = 0;
        float endX = 0.f;

    private:
    };
    ReplayResult replay(PlayLayer* layer, int snapshotSlot, int startTick,
                        std::vector<EbInputEvent> const& timeline, float deltaTime, int maxTicks,
                        bool shiftActionBy = false, int actionShift = 0);

    // Optional per-frame bake/diag trace (filled when traceFrames=true).
    class BakeFrameTrace {
    public:
        int frame = 0;
        float x = 0.f;
        float y = 0.f;
        float yVel = 0.f;
        bool jumpHeld = false;
        bool dead = false;
        bool unknown = false;
        bool twApplied = false;
        int eventsApplied = 0; // count this frame
        int mode = 0;
        int deathObjId = -1;
        int deathObjType = -1;
        float deathObjX = 0.f;
        float deathObjY = 0.f;

    private:
    };
    class BakeRunResult {
    public:
        bool died = false;
        bool unknown = false;
        int deathFrame = -1;
        float xAtSample = 0.f;
        float deathX = 0.f;
        float deathY = 0.f;
        GameModeKind modeAtSample = GameModeKind::Cube;
        bool dashingAtSample = false;
        bool sampled = false;
        int deathObjId = -1;
        int deathObjType = -1;
        int deathObjGameId = -1; // m_objectID of killer
        std::vector<BakeFrameTrace> frames;
        // Sorted by count desc (Commit 3). Filled whenever hist collection runs.
        std::vector<UnknownHistBin> unknownHist;
        int unknownSoftHits = 0;
        int unknownHardHits = 0;

    private:
    };
    // applyP2ToShadow: MegaHack often marks solo inputs as player2 — still feed P1 ghost.
    // traceFrames: record BakeFrameTrace each step (diag dumps).
    // parkReals: move live P1/P2 off-map during bake so spawn overlap cannot kill ghost.
    // substeps: optional micro-steps per bake tick (usually 1; bypass macros bake 1:1).
    // onFrame: optional progress hook; return false to cancel (bake validate).
    BakeRunResult runBakeTimeline(PlayLayer* layer, int snapshotSlot,
                                  std::vector<EbReplayEvent> const& events, float deltaTime,
                                  int untilFrame, int sampleFrame = -1, bool applyP2ToShadow = true,
                                  bool traceFrames = false, bool parkReals = true, int substeps = 1,
                                  std::function<bool(int frame)> onFrame = {});

    class PathSample {
    public:
        std::vector<cocos2d::CCPoint> points;
        int deathFrame = -1;
        bool died = false;

    private:
    };
    PathSample samplePath(PlayLayer* layer, int snapshotSlot,
                          std::vector<EbReplayEvent> const& events, float deltaTime, int untilFrame);

    class Model {
    public:
        bool continuous = false;
        bool tapRelease = false;
        bool stopAtBallLanding = false;
        int cap = 48;
        int slack = 2;
        // Live Delay-worthy: after zone clear keep sim until death / cap
        // (skip the short +24 early cut used for soft bake scoring).
        bool continuePastClear = false;

    private:
    };
    static Model modelFor(PlayerObject* player, int limit, float capMul = 1.f);

    class OrbWindow {
    public:
        bool found = false;
        int first = -1;
        int last = -1;

    private:
    };

private:
    friend class EbServiceStore;

    friend class EbShadowPlanInputWindow;
    friend class EbShadowPlanThreatProbe;
    friend class EbShadowPlanForesight;
    friend class EbShadowPlanCandidateSearch;
    friend class EbShadowPlanSelection;
    friend class EbShadowPlanResult;
    friend class EbShadowPlanSearch;
    friend class EbShadowPlanContext;

    friend class EbShadowGhostLifecycle;
    friend class EbShadowSectionSafety;
    friend class EbShadowWorldCapture;
    friend class EbShadowWorldDynamics;
    friend class EbShadowPlayerState;
    friend class EbShadowPhysics;
    friend class EbShadowCandidateRunner;
    friend class EbIdlePrefixShare;
    friend class EbIdlePrefixForkState;
    friend class EbAdaptiveResumeRunner;
    friend class EbAdaptivePlanSearch;
    friend class EbShadowPlanPolicy;
    friend class EbShadowDelayVerifier;
    friend class EbShadowSequencePlanner;
    friend class EbShadowReplayRunner;
    friend class EbShadowFreezeEvaluator;
    friend class EbShadowRegressionChecks;

    class SavedObject {
    public:
        EnhancedGameObject* obj = nullptr;
        bool activated = false;
        bool p1 = false;
        bool p2 = false;
        bool poweredOn = false;
        int state = 0;
        bool claimTouch = false;
        bool isRing = false;
        bool isActivated = false;
        bool egoVisible = true;
        GLubyte opacity = 255;
        bool nodeVisible = true;

    private:
    };

    // Transform snapshot for Trigger World Bake apply (restored before flags).
    class SavedTransform {
    public:
        int uniqueId = 0;
        GameObject* obj = nullptr;
        float x = 0.f;
        float y = 0.f;
        float rotation = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        bool nodeVisible = true;
        mutable bool dirty = false;
        // Last pose written this capture session — skip redundant writePose.
        mutable bool hasAppliedPose = false;
        mutable float appliedX = 0.f;
        mutable float appliedY = 0.f;
        mutable float appliedRotation = 0.f;
        mutable float appliedScaleX = 1.f;
        mutable float appliedScaleY = 1.f;
        mutable bool appliedActive = true;

    private:
    };

    // Cheap live DynamicFollow job (not written to static JSON).
    class DynamicFollowJob {
    public:
        int effectUniqueId = 0;
        EffectGameObject* effect = nullptr;
        bool lockX = false;
        bool lockY = false;
        bool followPlayerY = false;
        float offsetX = 0.f;
        float offsetY = 0.f;
        std::vector<SavedTransform> targets;

    private:
    };

    static void hideGhost(PlayerObject* player);
    static bool tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object);
    static void scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, EbShadowSimulator* self);

    RunResult runOne(PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
                     float deltaTime, float firstDt, Model const& model, int releaseAfter,
                     EbThreatZone const* zone, int* tickBudgetLeft);
    // Live V2 Trajectory engine: hold/coast button path (no orb pre-scan).
    // Same delay / releaseAfter / zone-settle contract as runOne for cand eval.
    RunResult runTrajectoryOne(PlayLayer* layer, PlayerObject* real, bool down, int delay,
                               int limit, float deltaTime, float firstDt, Model const& model,
                               int releaseAfter, EbThreatZone const* zone, int* tickBudgetLeft);
    RunResult runBaseline(PlayLayer* layer, PlayerObject* real, bool down, float deltaTime,
                          float firstDt, Model const& model, int* tickBudgetLeft);

    // holdJump=true: keep Jump held while coasting (press-to-reach orbs).
    // holdJump=false: Jump = !down (legacy coast / release scan).
    OrbWindow scanOrbWindow(PlayLayer* layer, PlayerObject* real, bool down, int scanTicks,
                            float deltaTime, float firstDt, int* tickBudgetLeft,
                            bool holdJump = false);

    static bool isHazardLike(GameObject* object);

    void beginCapture(PlayLayer* layer);
    void resetCaptureForNextRun(PlayLayer* layer);
    void restoreCapture(PlayLayer* layer);
    // Snapshot/restore world transforms for TriggerWorldStore ready tracks.
    void captureTriggerTransforms(PlayLayer* layer);
    void resetTriggerTransforms();
    void restoreTriggerTransforms(GJBaseGameLayer* layer);
    // Apply TriggerWorldStore poses for m_simAbsTick (near ghost first, capped).
    void applyTriggerWorldAtTick(PlayLayer* layer, int absTick, PlayerObject* ghost);
    // Scan player-coupled follow effects; record target ids (+ jobs if probe on).
    void captureDynamicFollows(PlayLayer* layer);
    // Cheap ghost-relative pose write when skip-dynamic is false (dual → no-op).
    void applyLiveDynamicNear(PlayLayer* layer, PlayerObject* ghost);
    void copyWaveTrail(PlayerObject* destination, PlayerObject* source);
    static void clearCollisionLogs(PlayerObject* state);
    void stepPreCollide(PlayLayer* layer, PlayerObject* state, float deltaTime);
    void stepPhysics(PlayLayer* layer, PlayerObject* state, float stepDt, float deltaTime);

    PlayerObject* m_shadow = nullptr;
    PlayerObject* m_snapshots[SNAPSHOT_SLOTS] = {};
    PlayerObject* m_evalGhosts[DEV_EVAL_SLOTS] = {};
    // Active body for markDead during evalDelayFromFreeze / replayActualFire.
    PlayerObject* m_evalActive = nullptr;
    bool m_evalAbort = false;
    // Layer that owns the ghost children. resetLevel runs during PlayLayer::init
    // (setupHasCompleted) before onLevelInit/attach — evacuate must no-op then.
    GJBaseGameLayer* m_hostLayer = nullptr;
    int m_nextSnapshot = 0;
    bool m_simulating = false;
    bool m_bakeOffline = false;
    bool m_applyingRing = false;
    bool m_orbScanning = false;
    bool m_orbOverlapTick = false;
    // Set when a ghost destroyPlayer was swallowed; cleared on real death / reset.
    bool m_suppressDeathHitboxes = false;
    // Restored in endSession so sim never leaves vanilla death-hitbox mode stuck.
    bool m_savedHitboxesOnDeath = false;
    bool m_savedP1Dead = false;
    bool m_savedP2Dead = false;
    bool m_dead = false;
    bool m_jumped = false;
    bool m_unknown = false;
    bool m_toggleUnknownHit = false;
    bool m_collectUnknownHist = false;
    bool m_activatedRing = false;
    int m_ringCount = 0;
    GameObject* m_deathObject = nullptr;
    // objectId → bin while m_collectUnknownHist (cleared each bake timeline).
    std::unordered_map<int, UnknownHistBin> m_unknownHistMap;
    std::vector<SavedObject> m_saved;
    std::vector<SavedTransform> m_savedTransforms;
    gd::map<std::pair<int, int>, int> m_savedActivatedIDs;
    // Absolute attempt tick for the current sim step (−1 = do not apply bake).
    int m_simAbsTick = -1;
    // Absolute tick corresponding to t=0 inside runOne / baseline / orb scan.
    int m_simTickBase = 0;
    // >0 during offline bake: TriggerWorld remap uses this dt, not live simDt.
    float m_poseRemapDt = -1.f;
    // Set when applyTriggerWorldAtTick runs for this step (SimCollisionPolicy).
    bool m_triggerWorldAppliedThisTick = false;
    // uniqueIds targeted by DynamicFollow (Unknown unless live probe applied).
    std::unordered_set<int> m_dynamicTargetIds;
    // Toggle orb 1594 / touch-1049 group members → always UnknownOnOverlap.
    std::unordered_set<int> m_toggleTargetIds;
    std::vector<DynamicFollowJob> m_dynamicFollowJobs;
    std::unordered_set<int> m_liveDynamicAppliedIds;
    // Reused by GameLayerHooks::collisionCheckObjects (sim + live ghost strip).
    gd::vector<GameObject*> m_collisionFilterBuf;

    void captureToggleUnknownTargets(PlayLayer* layer);

    static constexpr size_t kMaxDynamicFollowJobs = 32;
    static constexpr size_t kMaxDynamicTargetsPerJob = 64;
    // Perf: prefer objects near ghost X; stop after count or wall-clock budget.
    static constexpr size_t kMaxApplyPerTick = 96;
    static constexpr float kApplyMarginX = 420.f;
    static constexpr int kApplyBudgetUs = 350;
};

#endif
