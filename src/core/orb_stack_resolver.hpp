#ifndef ORB_STACK_RESOLVER_HPP
#define ORB_STACK_RESOLVER_HPP

#include <Geode/Geode.hpp>
#include <cstddef>
#include <string>
#include <vector>

class GameObject;
class PlayLayer;
class PlayerObject;
class RingObject;

// Commit 7 — Toggle+normal orb stack = BOTH must fire (vanilla clicks both).
// Sim early-return on 1594 used to drop the stacked jump orb; flip also used to
// hide jump rings in the toggle group. Rule: stacked partners always dual-fire.
// Same ring is never boosted twice (claim / touchedRings guards).
class OrbStackResolver {
public:
    // Match AssistContracts140 ε — stacked rings share a tile within noise.
    static constexpr float kColocateEps = 12.f;
    static constexpr size_t kMaxTouchLog = 64;

    class TouchEvent {
    public:
        int oid = 0;
        int uid = 0;
        bool toggle = false;
        bool jumpRing = false;
        bool jumpHeld = false;
        bool claimed = false;
        bool appliedJump = false;
        float x = 0.f;
        float y = 0.f;
        int tick = 0;

    private:
    };

    // Debug counters only — dual-fire is always on for stacks, not gated on these.
    class Stats {
    public:
        int touchCount = 0;
        int toggleTouches = 0;
        int jumpTouches = 0;
        int stackDualFires = 0; // partner fired because of a stack
        int flipSkipped = 0;    // jump/dash members skipped in group flip

    private:
    };

    static OrbStackResolver& get();

    void begin();
    Stats const& stats() const {
        return this->m_stats;
    }
    std::vector<TouchEvent> const& touchLog() const {
        return this->m_log;
    }

    static bool isJumpOrDashRingType(GameObjectType type);
    static bool isJumpOrDashRing(GameObject* object);

    // Jump/dash rings (and ε-near rings) must not be flipped with the toggle group.
    static bool shouldSkipToggleFlip(GameObject* member, GameObject* toggleHit);
    void noteFlipSkipped();

    static bool withinColocateEps(GameObject* firstValue, GameObject* secondValue);

    void noteTouch(PlayerObject* shadow, RingObject* ring, bool appliedJump, int simTick);

    // After either partner is touched: fire the other stacked partner(s).
    // Toggle hit → applyRingJump on co-located normal orbs (Jump held).
    // Normal orb hit → onToggleHit on co-located toggle orbs still unclaimed.
    int fireStackedPartners(PlayLayer* layer, PlayerObject* shadow, GameObject* hit);

    static bool selfCheck(std::string& report);

private:
    OrbStackResolver() = default;

    int fireNormalOrbsAt(PlayLayer* layer, PlayerObject* shadow, GameObject* toggleHit);
    int fireTogglesAt(PlayLayer* layer, PlayerObject* shadow, GameObject* jumpHit);

    Stats m_stats;
    std::vector<TouchEvent> m_log;
};

#endif
