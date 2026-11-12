#ifndef DEV_LIVE_WITNESS_HPP
#define DEV_LIVE_WITNESS_HPP

#include "dev_clock_contract.hpp"
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class PlayLayer;
class PlayerObject;
class EbSimPlan;

// Observation quality per category (Vertrag B).
// At capture time: Restored = complete restorable snapshot held in the witness.
// Commit 8's restore/diff may confirm or demote this.
enum class DevWitnessStatus { Absent = 0, DigestOnly, Partial, Restored };

enum class DevWitnessCategory { Player = 0, Dual, Clock, Orbs, Triggers, Movers, CollisionFlags };

// Nearby object pose for freeze-world restore (Commit 11).
// Hashes stay DigestOnly; these poses can upgrade Movers to Restored.
class EbDevObjectPose {
public:
    int uniqueId = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool active = true;

private:
};

// Dev Lab AP6: axis-aligned rect (GameObject::getObjectRect).
class EbDevAabb {
public:
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;

private:
};

// Top-N hazard neighborhood (id/type/AABB) for offline ghost-hitbox research.
class EbDevHazardSnap {
public:
    int uniqueId = 0;
    int objectType = 0;
    float x = 0.f;
    float y = 0.f;
    EbDevAabb aabb{};
    float dist2 = 0.f;

private:
};

// Plan-time collide rects (live + optional shadow) + overlap counts vs hazard list.
class EbDevHitboxSnap {
public:
    bool present = false;
    EbDevAabb live{};
    bool hasGhost = false;
    EbDevAabb ghost{};
    int hazardOverlapLive = 0;
    int hazardOverlapGhost = 0;

private:
};

// AP9: serializable contact / collide scalars (pointer refs → uniqueId only).
// Keeps CollisionFlags Partial honest without a full diag dump.
class EbDevContactSnap {
public:
    bool present = false;
    bool maybeIsColliding = false;
    bool collidingWithLeft = false;
    bool collidingWithRight = false;
    bool lastCollisionBottom = false;
    bool lastCollisionTop = false;
    bool lastCollisionLeft = false;
    bool lastCollisionRight = false;
    float collidedTopMinY = 0.f;
    float collidedBottomMaxY = 0.f;
    float collidedLeftMaxX = 0.f;
    float collidedRightMinX = 0.f;
    float snapDistance = 0.f;
    float groundYVelocity = 0.f;
    int groundObjectMaterial = 0;
    int collidedObjectId = 0;
    int lastGroundObjectId = 0;
    int objectSnappedToId = 0;
    bool isOnIce = false;
    bool isSliding = false;

private:
};

inline bool operator==(EbDevContactSnap const& a, EbDevContactSnap const& b) {
    return a.present == b.present && a.maybeIsColliding == b.maybeIsColliding &&
           a.collidingWithLeft == b.collidingWithLeft &&
           a.collidingWithRight == b.collidingWithRight &&
           a.lastCollisionBottom == b.lastCollisionBottom &&
           a.lastCollisionTop == b.lastCollisionTop && a.lastCollisionLeft == b.lastCollisionLeft &&
           a.lastCollisionRight == b.lastCollisionRight &&
           a.collidedTopMinY == b.collidedTopMinY && a.collidedBottomMaxY == b.collidedBottomMaxY &&
           a.collidedLeftMaxX == b.collidedLeftMaxX && a.collidedRightMinX == b.collidedRightMinX &&
           a.snapDistance == b.snapDistance && a.groundYVelocity == b.groundYVelocity &&
           a.groundObjectMaterial == b.groundObjectMaterial &&
           a.collidedObjectId == b.collidedObjectId && a.lastGroundObjectId == b.lastGroundObjectId &&
           a.objectSnappedToId == b.objectSnappedToId && a.isOnIce == b.isOnIce &&
           a.isSliding == b.isSliding;
}

inline bool operator!=(EbDevContactSnap const& a, EbDevContactSnap const& b) {
    return !(a == b);
}

inline bool operator==(EbDevObjectPose const& a, EbDevObjectPose const& b) {
    return a.uniqueId == b.uniqueId && a.x == b.x && a.y == b.y && a.rotation == b.rotation &&
           a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.active == b.active;
}

inline bool operator!=(EbDevObjectPose const& a, EbDevObjectPose const& b) {
    return !(a == b);
}

// Serializable physics subset of ShadowSimulator::copyPlayerState.
// Pointer/object-ref fields are intentionally absent (see collisionFlags).
class EbDevPlayerBlob {
public:
    bool present = false;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float yVelocity = 0.f;
    float fallSpeed = 0.f;
    float gravity = 0.f;
    float gravityMod = 0.f;
    float speedMultiplier = 0.f;
    float playerSpeed = 0.f;
    float vehicleSize = 0.f;
    float platformerXVelocity = 0.f;
    bool isUpsideDown = false;
    bool isSideways = false;
    bool isGoingLeft = false;
    bool isShip = false;
    bool isBall = false;
    bool isBird = false;
    bool isSwing = false;
    bool isDart = false;
    bool isRobot = false;
    bool isSpider = false;
    bool isSecondPlayer = false;
    bool isPlatformer = false;
    bool isDashing = false;
    bool isOnGround = false;
    bool isOnGround2 = false;
    bool isOnSlope = false;
    bool wasOnSlope = false;
    bool jumpBuffered = false;
    bool wasJumpBuffered = false;
    bool stateRingJump = false;
    bool hasEverJumped = false;
    bool hasEverHitRing = false;
    bool isLocked = false;
    bool controlsDisabled = false;
    bool maybeIsFalling = false;
    bool maybeIsBoosted = false;
    // AP7: continuous / hold honesty at plan capture.
    bool jumpHeld = false;

private:
};

class EbDevWitnessCategoryEntry {
public:
    DevWitnessCategory category = DevWitnessCategory::Player;
    DevWitnessStatus status = DevWitnessStatus::Absent;
    uint64_t digest = 0;         // hazard/AABB/object-set hashes — always DigestOnly
    bool digestMismatch = false; // reserved for later drift checks

private:
};

// Live observation at plan time. No restore in Commit 7.
class EbDevLiveWitness {
public:
    // Bump when the serializable field list of DevPlayerBlob changes.
    static constexpr int kPlayerBlobSchemaVersion = 2;

    std::vector<EbDevWitnessCategoryEntry> categories;
    std::unordered_set<int> worldDeps; // DevWitnessCategory as int
    EbDevPlayerBlob player{};
    EbDevPlayerBlob dual{};
    EbDevClockContract::Stamp clock{};
    std::vector<int> orbIds;
    std::vector<EbDevObjectPose> moverPoses; // only when freezeWorld
    std::string triggerStoreKey;
    int triggerStoreVersion = 0;
    int triggerApplyTick = -1;
    bool triggerCanSpool = false;
    // Trigger-World bake / store snapshot for Dev Lab (Commit 1 / 1.3.1).
    int triggerReadyTracks = 0;
    int triggerTrackCount = 0;
    int triggerBakeEffects = 0;
    int triggerBakeChildren = 0;
    int triggerCollectedEffects = 0;
    int triggerSkipFollow = 0;
    int triggerSkipDynamic = 0;
    int triggerSkipNonSpawn = 0;
    int triggerSkipTouch = 0;
    uint64_t playerBlobSchema = 0;
    uint64_t hazardDigest = 0;
    bool hasHazardDigest = false;
    // AP6: structured hazard neighborhood + hitbox (when recordHazards).
    static constexpr int kMaxHazardSnaps = 24;
    std::vector<EbDevHazardSnap> hazards;
    bool hasHazards = false;
    EbDevHitboxSnap hitbox{};
    bool hasHitbox = false;
    // AP9: contact/collide parity (always when player present).
    EbDevContactSnap contact{};
    bool hasContact = false;
    EbDevContactSnap dualContact{};
    bool hasDualContact = false;
    bool hasThreatZone = false;
    float threatZoneStartX = 0.f;
    float threatZoneEndX = 0.f;
    bool freezeWorld = false;
    bool valid = false;

    static char const* categoryId(DevWitnessCategory candidate);
    static bool parseCategory(std::string const& s, DevWitnessCategory& out);
    static char const* statusId(DevWitnessStatus state);
    static bool parseStatus(std::string const& s, DevWitnessStatus& out);

    // Ordered field-name catalog → stable schema hash.
    static uint64_t computePlayerBlobSchema();
    static char const* const* playerBlobFieldNames(size_t& count);

    static DevWitnessStatus categoryStatus(EbDevLiveWitness const& w, DevWitnessCategory candidate);
    static bool hasWorldDep(EbDevLiveWitness const& w, DevWitnessCategory candidate);

    // Capture from live play. player required for a useful witness.
    static EbDevLiveWitness capture(PlayLayer* layer, PlayerObject* player, PlayerObject* player2,
                                  EbSimPlan const& plan, EbDevClockContract::Stamp const& clock,
                                  int effectTick, bool freezeWorld);

    // worldDeps from SimPlan flags + dual mode (not post-death heuristics).
    // dualMode tags Dual even when P2 is missing → honest Absent, not silent.
    static void fillWorldDeps(EbDevLiveWitness& w, EbSimPlan const& plan, bool dualMode);

    static bool selfCheck(std::string& report);

    EbDevLiveWitness() = default;

private:
    static void fillPlayerBlob(EbDevPlayerBlob& blob, PlayerObject* source);
    static void fillContactSnap(EbDevContactSnap& snap, PlayerObject* source);
    static void setCategory(EbDevLiveWitness& w, DevWitnessCategory candidate,
                            DevWitnessStatus status, uint64_t digest = 0);
    static uint64_t hashHazardNeighborhood(PlayLayer* layer, PlayerObject* player, float radiusX);
    static void captureHazardSnaps(EbDevLiveWitness& w, PlayLayer* layer, PlayerObject* player,
                                   float radiusX);
    static void captureHitboxSnap(EbDevLiveWitness& w, PlayLayer* layer, PlayerObject* player);
};

#include <Geode/Geode.hpp>
#include <cmath>
#include <cstring>
#include <fmt/format.h>

class EbDevLiveWitnessImplementation {
public:
    static uint64_t fnv1aMix(uint64_t height, uint64_t value);
    static uint64_t fnv1aBytes(void const* data, size_t count);
    static uint64_t fnv1aStr(char const* state);
    static bool hazardLikeObject(GameObject* object);

private:
    EbDevLiveWitnessImplementation() = delete;
};

#endif
