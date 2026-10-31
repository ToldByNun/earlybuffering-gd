#ifndef EARLYBUFFER_CORE_TOGGLEORBSIM_HPP
#define EARLYBUFFER_CORE_TOGGLEORBSIM_HPP

#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class GameObject;
class PlayLayer;
class PlayerObject;

// Reversible group on/off for toggle orb 1594 / touch-toggle 1049 during shadow sim.
// Does NOT call vanilla triggerObject — only collision-relevant pose fields.
// Restore on every capture reset so live objects never keep sim flips.
class ToggleOrbSim {
public:
    struct CollisionSnap {
        GameObject* obj = nullptr;
        int uniqueId = 0;
        bool poweredOn = false;
        bool activated = false;
        bool isActivated = false;
        bool egoVisible = false;
        bool nodeVisible = false;
        int opacity = 255;
        bool wantActive = false;
        bool isRing = false;
        bool claimTouch = false;
    };

    static ToggleOrbSim& get();

    void begin();
    void restore();

    // Shadow hit a toggle orb / touch-toggle. Flips group members; claims effect.
    // Returns true if at least one member was modeled.
    bool onToggleHit(PlayLayer* layer, PlayerObject* shadow, GameObject* hit);

    bool isModeled(int uniqueId) const;
    bool isModeledActive(int uniqueId) const;

    // Shared with TriggerWorldStore::writePose — collision off is more than setVisible.
    static void applyCollisionActive(GameObject* obj, bool active);
    static void captureCollision(GameObject* obj, CollisionSnap& out);
    static void restoreCollision(CollisionSnap const& snap);

    static bool selfCheck(std::string& report);

private:
    ToggleOrbSim() = default;

    static constexpr size_t kMaxFlipMembers = 512;
    static constexpr size_t kMaxClaimedEffects = 64;

    std::vector<CollisionSnap> m_snaps;
    // uniqueId → post-flip active (collision Include vs Skip).
    std::unordered_map<int, bool> m_modeledActive;
    std::unordered_set<int> m_claimedEffects;
};

#endif
