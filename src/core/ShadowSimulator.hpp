#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>

namespace frire {

enum class PlanAction {
    Pass,     // do the input now (vanilla) — NEVER block the player
    Delay     // wait `delay` ticks, then do it (assist only)
};

struct PlanOptions {
    float capMul = 1.f;
    int maxBranches = 4;
    int budgetUs = 1500; // 0 = no budget
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
    bool unknown = false;
    bool budgetHit = false;
};

struct InputEvent {
    int tick = 0; // absolute attempt tick
    bool down = false;
};

// On a press or release: for each offset 0..limit, run the real trajectory.
// Until that offset the button stays as it is now (not yet pressed / still held).
// At that offset the input happens, then movement, then hitboxes.
// Follow-up branches (release after k) can still rescue a continuous death.
// The first offset that does not die wins. Offset 0 → Pass (do it now).
// A later offset → Delay (block the original input, inject it there).
// If every offset dies → Pass. Never blocks a click that cannot be saved.
class ShadowSimulator {
public:
    static constexpr int SNAPSHOT_SLOTS = 16;

    static ShadowSimulator& get();

    void attach(PlayLayer* layer);
    void detach();

    bool isSimulating() const { return m_simulating; }
    bool isShadow(PlayerObject* p) const { return p && p == m_shadow; }
    PlayerObject* shadow() const { return m_shadow; }
    bool isShadowTrail(HardStreak* trail) const {
        return m_shadow && trail && trail == m_shadow->m_waveTrail;
    }

    void markDead(PlayerObject* p, GameObject* object = nullptr);
    void noteJump();
    void noteActivated(GameObject* obj);
    void noteUnknown() { m_unknown = true; }

    // Apply orb physics to the shadow and mark the orb used for THIS run only
    // (restored afterward). Prevents the same orb from re-firing every tick.
    void applyRingJump(PlayerObject* shadow, RingObject* ring);
    bool isApplyingRing() const { return m_applyingRing; }

    // Copy all physics-relevant state from src onto dst (shadow or snapshot slot).
    static void copyPlayerState(PlayerObject* dst, PlayerObject* src);

    // Capture real player into a ring-buffer snapshot slot. Returns slot index.
    int captureSnapshot(PlayerObject* real);
    PlayerObject* snapshotSlot(int slot) const;

    SimPlan plan(
        PlayLayer* layer, PlayerObject* real, bool down, int limit,
        float dt, float firstDt, PlanOptions const& options = {}
    );

    // Replay from a snapshot with an absolute-tick input timeline.
    // Returns death tick relative to startTick, or -1 if survived.
    // deathObject out-param optional.
    struct ReplayResult {
        bool died = false;
        int deathTick = -1; // relative to startTick
        GameObject* deathObject = nullptr;
        bool unknown = false;
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

    struct Model {
        bool continuous = false;
        bool tapRelease = false;
        int cap = 48;
        int slack = 2;
    };
    static Model modelFor(PlayerObject* p, int limit, float capMul = 1.f);

private:
    struct RunResult {
        bool died = false;
        bool diedBeforeAction = false;
        bool jumped = false;
        bool unknown = false;
        int jumpTick = -1;
        int deathTick = -1;
        int unknownTick = -1;
        GameObject* deathObject = nullptr;
    };

    struct SavedObject {
        EnhancedGameObject* obj = nullptr;
        bool activated = false;
        bool p1 = false;
        bool p2 = false;
        bool poweredOn = false;
        int state = 0;
        bool claimTouch = false;
        bool isRing = false;
        bool isActivated = false;   // GameObject::m_isActivated
        bool egoVisible = true;     // EnhancedGameObject::m_visible
        GLubyte opacity = 255;
        bool nodeVisible = true;
    };

    static void hideGhost(PlayerObject* p);

    // releaseAfter < 0 → hold until end (or tap-release for cube).
    // releaseAfter >= 0 → release that many ticks after the action.
    RunResult runOne(
        PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
        float dt, float firstDt, Model const& model, int releaseAfter
    );
    // Baseline: never apply the new input.
    RunResult runBaseline(
        PlayLayer* layer, PlayerObject* real, bool down,
        float dt, float firstDt, Model const& model
    );

    void beginCapture(PlayLayer* layer);
    void restoreCapture(PlayLayer* layer);
    void copyWaveTrail(PlayerObject* dst, PlayerObject* src);
    static void clearCollisionLogs(PlayerObject* s);
    // Hitboxes at the current position (before this tick's input/move).
    void stepPreCollide(PlayLayer* layer, PlayerObject* s, float dt);
    // Move, then hitboxes at the new position.
    void stepPhysics(PlayLayer* layer, PlayerObject* s, float stepDt, float dt);

    PlayerObject* m_shadow = nullptr;
    PlayerObject* m_snapshots[SNAPSHOT_SLOTS] = {};
    int m_nextSnapshot = 0;
    bool m_simulating = false;
    bool m_applyingRing = false;
    bool m_dead = false;
    bool m_jumped = false;
    bool m_unknown = false;
    GameObject* m_deathObject = nullptr;
    std::vector<SavedObject> m_saved;
    gd::map<std::pair<int, int>, int> m_savedActivatedIDs;
};

}
