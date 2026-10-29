#ifndef EARLYBUFFER_CORE_SHADOWSIMULATOR_HPP
#define EARLYBUFFER_CORE_SHADOWSIMULATOR_HPP

#include "ReplayImport.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>


enum class PlanAction {
    Pass,     // do the input now (vanilla) — NEVER block the player
    Delay     // wait `delay` ticks, then do it (assist only)
};

struct PlanOptions {
    float capMul = 1.f;
    int maxBranches = 4;
    int budgetUs = 1500; // soft cap; tick budget is primary
    int tickBudget = 4000; // physics ticks per plan() call
    int smartDepth = 1;
    int smartCombos = 48;
    int smartHorizon = 0; // 0 = auto from model cap
    int attemptStartTick = 0; // absolute attempt tick at plan time (for abs death ticks)
    // Live bake-prior: delay ticks from a nearby BakeMap window (−1 = unset).
    // Enqueued after d0 / orb window; early-exit when a ±k-robust prefer delay is viable.
    int preferDelayMin = -1;
    int preferDelayMax = -1;
    // ±k robust margin: 0 = auto (mode/speed/dtVariance/spam); else clamp 1–3.
    int robustK = 0;
    bool robustJitter = false;
    float dtVariance = 0.f; // relative dt EMA from AssistController (auto k bump)
    // betterCand after surviveBin: endX − latencyWeight · delay.
    float latencyWeight = 0.25f;
    // ClickIntentGuard density (assist still running, e.g. orb) → wider auto k,
    // slightly softer Wave λ so earliest-delay bias does not phase-shift spam.
    bool spamDense = false;
};

struct ThreatZone {
    float startX = 0.f;
    float endX = 0.f;
    bool valid = false;
};

struct ActWindow {
    int dMin = -1;
    int dMax = -1;
    bool valid() const { return dMin >= 0 && dMax >= dMin; }
};

struct SimPlan {
    PlanAction action = PlanAction::Pass;
    int delay = 0;
    int jumpTick = 0;
    bool nowIsSafe = false;
    bool continuous = false;
    int candidatesTried = 0;
    int firstDeathTick = -1;
    GameObject* firstDeathObject = nullptr;
    int baselineDeathTick = -1;
    GameObject* baselineDeathObject = nullptr;
    float baselineDeathX = 0.f;
    bool unknown = false;
    bool budgetHit = false;
    bool activatedRing = false;
    bool usedOrbWindow = false;
    bool threatCleared = false;
    bool orbUseful = false;
    bool requireOrb = false;
    bool requireHold = false;
    int dMin = -1;
    int dMax = -1;
    // Hold/release after assisted press (ticks after the press action).
    // earliest..latest = safe hold durations; preferred = best release timing.
    // -1 = unset (no probe / Pass).
    int releaseAfter = -1;
    int releaseEarliest = -1;
    int releaseLatest = -1;
    int holdScanTried = 0;
    int planId = 0;
    int smartDepthUsed = 1;
    int smartCombosUsed = 0;
    ThreatZone threatZone{};
};

struct InputEvent {
    int tick = 0; // absolute attempt tick
    bool down = false;
};

// Lexicographic planner: threat-zone clear, orb activation windows, adaptive ±k
// robustness, optional firstDt jitter, latency-weighted endX. Never blocks (Pass).
class ShadowSimulator {
public:
    static constexpr int SNAPSHOT_SLOTS = 16;
    static constexpr float kClusterGapX = 50.f;
    static constexpr int kWindowMarginK = 1;
    static constexpr int kDefaultTickBudget = 4000;
    // Wave zigzag: hazard AABB within this ahead-X (+ auto from limit) → Pass.
    static constexpr float kHazardProximityXMin = 70.f;
    static constexpr float kHazardProximityXMax = 140.f;
    // Delays that do not improve surviveBin / endX beyond this → Pass (wave).
    static constexpr float kFutileEndXGain = 8.f;

    static ShadowSimulator& get();

    void attach(PlayLayer* layer);
    void detach(bool layerDying = false);

    bool isSimulating() const { return m_simulating; }
    // True after applyTriggerWorldAtTick ran for the current sim step.
    bool triggerWorldAppliedThisTick() const {
        return this->m_triggerWorldAppliedThisTick;
    }
    // Object is targeted by a player-coupled DynamicFollow (sim collision gate).
    bool isDynamicTriggerTarget(int uniqueId) const;
    // Cheap live probe wrote a pose for this id on the current sim step.
    bool liveDynamicApplied(int uniqueId) const;
    bool isShadow(PlayerObject* p) const { return p && p == m_shadow; }
    // Shadow + snapshot slots — never feed these into vanilla destroyPlayer.
    bool isOwnedGhost(PlayerObject* p) const;
    PlayerObject* shadow() const { return m_shadow; }
    bool isShadowTrail(HardStreak* trail) const {
        return m_shadow && trail && trail == m_shadow->m_waveTrail;
    }

    void markDead(PlayerObject* p, GameObject* object = nullptr);
    // Park ghosts off-level so stray collision cannot fail the real attempt.
    void parkGhosts();
    // Before PlayLayer::resetLevel / checkpoint reload: pull every owned ghost out of
    // section vectors (sim checkCollisions inserts them; vanilla sortSectionVector AVs).
    void evacuateGhosts(GJBaseGameLayer* layer);
    void noteJump();
    void noteActivated(GameObject* obj);
    void noteUnknown() { m_unknown = true; }

    void applyRingJump(PlayerObject* shadow, RingObject* ring);
    bool isApplyingRing() const { return m_applyingRing; }
    bool isOrbScanning() const { return m_orbScanning; }
    void noteOrbOverlap();

    static void copyPlayerState(PlayerObject* dst, PlayerObject* src);

    int captureSnapshot(PlayerObject* real);
    PlayerObject* snapshotSlot(int slot) const;

    SimPlan plan(
        PlayLayer* layer, PlayerObject* real, bool down, int limit,
        float dt, float firstDt, PlanOptions const& options = {}
    );

    SimPlan planSequence(
        PlayLayer* layer, PlayerObject* real, bool down, int limit,
        float dt, float firstDt, PlanOptions const& options = {}
    );

    // Single-candidate bake check: runOne(delay) must clear the threat zone
    // and not die before the action. Used by Hybrid bake verify (O(1) sim).
    struct VerifyDelayResult {
        bool ok = false;
        bool clearsThreat = false;
        bool diedBeforeAction = false;
        bool budgetHit = false;
        bool unknown = false;
        ThreatZone threatZone{};
    };
    VerifyDelayResult verifyDelay(
        PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
        float dt, float firstDt, PlanOptions const& options = {}
    );

    // Build hazard cluster around a death X / object (public for tests/logging).
    static ThreatZone buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj);

    // Ahead-X / jump-reach orb hint (used by bake heuristic + planner).
    static bool spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit, float dt);

    // Hazard AABB within proximityX ahead of player (wave corridor spikes).
    static bool spatialHazardProximity(
        PlayLayer* layer, PlayerObject* real, float proximityX
    );

    // Pure gates for plan() / selfCheck (no layer).
    static bool shouldWaveHazardProximityPass(
        bool isWave, bool nearHazard, bool noOrbTarget
    );
    static bool shouldWaveFutileDelayPass(
        bool isWave, bool orbIntent, int bestSurviveBin, float bestEndXGain
    );

    // Prefer short release for continuous; mid-window for tap modes.
    static int pickPreferredRelease(int earliest, int latest, bool continuous);

    // Auto robustK (no setting override). Wave spam → ≥2, often 3 with speed.
    static int autoRobustK(
        PlayerObject* real, float dtVariance, bool spamDense = false
    );
    // Pure hints for selfCheck (no PlayerObject).
    static int autoRobustKHints(
        bool waveLike,
        bool isWave,
        float playerSpeed,
        float dtVariance,
        bool spamDense
    );

    // Headless regression checks (threat bins / zone clear math). Returns false on fail.
    static bool regressionSelfCheck(std::string& report);

    struct ReplayResult {
        bool died = false;
        int deathTick = -1;
        GameObject* deathObject = nullptr;
        bool unknown = false;
        bool activatedRing = false;
        int ringCount = 0;
        float endX = 0.f;
    };
    ReplayResult replay(
        PlayLayer* layer,
        int snapshotSlot,
        int startTick,
        std::vector<InputEvent> const& timeline,
        float dt,
        int maxTicks,
        bool shiftActionBy = false,
        int actionShift = 0
    );

    struct BakeRunResult {
        bool died = false;
        int deathFrame = -1;
        float xAtSample = 0.f;
        GameModeKind modeAtSample = GameModeKind::Cube;
        bool sampled = false;
    };
    BakeRunResult runBakeTimeline(
        PlayLayer* layer,
        int snapshotSlot,
        std::vector<ReplayEvent> const& events,
        float dt,
        int untilFrame,
        int sampleFrame = -1
    );

    struct PathSample {
        std::vector<cocos2d::CCPoint> points;
        int deathFrame = -1;
        bool died = false;
    };
    PathSample samplePath(
        PlayLayer* layer,
        int snapshotSlot,
        std::vector<ReplayEvent> const& events,
        float dt,
        int untilFrame
    );

    struct Model {
        bool continuous = false;
        bool tapRelease = false;
        int cap = 48;
        int slack = 2;
    };
    static Model modelFor(PlayerObject* p, int limit, float capMul = 1.f);

    struct OrbWindow {
        bool found = false;
        int first = -1;
        int last = -1;
    };

    struct RunResult {
        bool died = false;
        bool diedBeforeAction = false;
        bool jumped = false;
        bool unknown = false;
        bool activatedRing = false;
        bool unknownTrunc = false;
        bool zoneCleared = false;
        bool wasDashing = false;
        int ringCount = 0;
        int jumpTick = -1;
        int deathTick = -1;
        int unknownTick = -1;
        int ticksRan = 0;
        int ticksBeforeNeutral = 0;
        float endX = 0.f;
        float deathX = 0.f;
        GameObject* deathObject = nullptr;
    };

private:
    struct SavedObject {
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
    };

    // Transform snapshot for Trigger World Bake apply (restored before flags).
    struct SavedTransform {
        int uniqueId = 0;
        GameObject* obj = nullptr;
        float x = 0.f;
        float y = 0.f;
        float rotation = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        bool nodeVisible = true;
    };

    // Cheap live DynamicFollow job (not written to static JSON).
    struct DynamicFollowJob {
        int effectUniqueId = 0;
        EffectGameObject* effect = nullptr;
        bool lockX = false;
        bool lockY = false;
        bool followPlayerY = false;
        float offsetX = 0.f;
        float offsetY = 0.f;
        std::vector<SavedTransform> targets;
    };

    static void hideGhost(PlayerObject* p);
    static bool tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* obj);
    static void scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, ShadowSimulator* self);

    RunResult runOne(
        PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
        float dt, float firstDt, Model const& model, int releaseAfter,
        ThreatZone const* zone, int* tickBudgetLeft
    );
    RunResult runBaseline(
        PlayLayer* layer, PlayerObject* real, bool down,
        float dt, float firstDt, Model const& model, int* tickBudgetLeft
    );

    OrbWindow scanOrbWindow(
        PlayLayer* layer, PlayerObject* real, bool down,
        int scanTicks, float dt, float firstDt, int* tickBudgetLeft
    );

    static bool isHazardLike(GameObject* obj);

    void beginCapture(PlayLayer* layer);
    void restoreCapture(PlayLayer* layer);
    // Snapshot/restore world transforms for TriggerWorldStore ready tracks.
    void captureTriggerTransforms(PlayLayer* layer);
    void restoreTriggerTransforms(GJBaseGameLayer* layer);
    // Apply TriggerWorldStore poses for m_simAbsTick using capture-list pointers.
    void applyTriggerWorldAtTick(PlayLayer* layer, int absTick);
    // Scan player-coupled follow effects; record target ids (+ jobs if probe on).
    void captureDynamicFollows(PlayLayer* layer);
    // Cheap ghost-relative pose write when skip-dynamic is false (dual → no-op).
    void applyLiveDynamicNear(PlayLayer* layer, PlayerObject* ghost);
    void copyWaveTrail(PlayerObject* dst, PlayerObject* src);
    static void clearCollisionLogs(PlayerObject* s);
    void stepPreCollide(PlayLayer* layer, PlayerObject* s, float dt);
    void stepPhysics(PlayLayer* layer, PlayerObject* s, float stepDt, float dt);

    PlayerObject* m_shadow = nullptr;
    PlayerObject* m_snapshots[SNAPSHOT_SLOTS] = {};
    // Layer that owns the ghost children. resetLevel runs during PlayLayer::init
    // (setupHasCompleted) before onLevelInit/attach — evacuate must no-op then.
    GJBaseGameLayer* m_hostLayer = nullptr;
    int m_nextSnapshot = 0;
    bool m_simulating = false;
    bool m_applyingRing = false;
    bool m_orbScanning = false;
    bool m_orbOverlapTick = false;
    bool m_dead = false;
    bool m_jumped = false;
    bool m_unknown = false;
    bool m_activatedRing = false;
    int m_ringCount = 0;
    GameObject* m_deathObject = nullptr;
    std::vector<SavedObject> m_saved;
    std::vector<SavedTransform> m_savedTransforms;
    gd::map<std::pair<int, int>, int> m_savedActivatedIDs;
    // Absolute attempt tick for the current sim step (−1 = do not apply bake).
    int m_simAbsTick = -1;
    // Absolute tick corresponding to t=0 inside runOne / baseline / orb scan.
    int m_simTickBase = 0;
    // Set when applyTriggerWorldAtTick runs for this step (SimCollisionPolicy).
    bool m_triggerWorldAppliedThisTick = false;
    // uniqueIds targeted by DynamicFollow (Unknown unless live probe applied).
    std::unordered_set<int> m_dynamicTargetIds;
    std::vector<DynamicFollowJob> m_dynamicFollowJobs;
    std::unordered_set<int> m_liveDynamicAppliedIds;

    static constexpr size_t kMaxDynamicFollowJobs = 32;
    static constexpr size_t kMaxDynamicTargetsPerJob = 64;
};

#endif // EARLYBUFFER_CORE_SHADOWSIMULATOR_HPP
