#ifndef DEV_LIVE_WITNESS_HPP
#define DEV_LIVE_WITNESS_HPP

#include "DevClockContract.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class PlayLayer;
class PlayerObject;
struct SimPlan;

// Observation quality per category (Vertrag B).
// At capture time: Restored = complete restorable snapshot held in the witness.
// Commit 8's restore/diff may confirm or demote this.
enum class DevWitnessStatus {
    Absent = 0,
    DigestOnly,
    Partial,
    Restored
};

enum class DevWitnessCategory {
    Player = 0,
    Dual,
    Clock,
    Orbs,
    Triggers,
    Movers,
    CollisionFlags
};

// Nearby object pose for freeze-world restore (Commit 11).
// Hashes stay DigestOnly; these poses can upgrade Movers to Restored.
struct DevObjectPose {
    int uniqueId = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool active = true;
};

inline bool operator==(DevObjectPose const& a, DevObjectPose const& b) {
    return a.uniqueId == b.uniqueId
        && a.x == b.x && a.y == b.y
        && a.rotation == b.rotation
        && a.scaleX == b.scaleX && a.scaleY == b.scaleY
        && a.active == b.active;
}

inline bool operator!=(DevObjectPose const& a, DevObjectPose const& b) {
    return !(a == b);
}

// Serializable physics subset of ShadowSimulator::copyPlayerState.
// Pointer/object-ref fields are intentionally absent (see collisionFlags).
struct DevPlayerBlob {
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
};

struct DevWitnessCategoryEntry {
    DevWitnessCategory category = DevWitnessCategory::Player;
    DevWitnessStatus status = DevWitnessStatus::Absent;
    uint64_t digest = 0; // hazard/AABB/object-set hashes — always DigestOnly
    bool digestMismatch = false; // reserved for later drift checks
};

// Live observation at plan time. No restore in Commit 7.
class DevLiveWitness {
public:
    // Bump when the serializable field list of DevPlayerBlob changes.
    static constexpr int kPlayerBlobSchemaVersion = 1;

    std::vector<DevWitnessCategoryEntry> categories;
    std::unordered_set<int> worldDeps; // DevWitnessCategory as int
    DevPlayerBlob player{};
    DevPlayerBlob dual{};
    DevClockContract::Stamp clock{};
    std::vector<int> orbIds;
    std::vector<DevObjectPose> moverPoses; // only when freezeWorld
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
    uint64_t playerBlobSchema = 0;
    uint64_t hazardDigest = 0;
    bool hasHazardDigest = false;
    bool freezeWorld = false;
    bool valid = false;

    static char const* categoryId(DevWitnessCategory c);
    static bool parseCategory(std::string const& s, DevWitnessCategory& out);
    static char const* statusId(DevWitnessStatus s);
    static bool parseStatus(std::string const& s, DevWitnessStatus& out);

    // Ordered field-name catalog → stable schema hash.
    static uint64_t computePlayerBlobSchema();
    static char const* const* playerBlobFieldNames(size_t& count);

    static DevWitnessStatus categoryStatus(
        DevLiveWitness const& w, DevWitnessCategory c
    );
    static bool hasWorldDep(DevLiveWitness const& w, DevWitnessCategory c);

    // Capture from live play. player required for a useful witness.
    static DevLiveWitness capture(
        PlayLayer* layer,
        PlayerObject* player,
        PlayerObject* player2,
        SimPlan const& plan,
        DevClockContract::Stamp const& clock,
        int effectTick,
        bool freezeWorld
    );

    // worldDeps from SimPlan flags + dual presence (not post-death heuristics).
    static void fillWorldDeps(
        DevLiveWitness& w,
        SimPlan const& plan,
        bool dualActive
    );

    static bool selfCheck(std::string& report);

    DevLiveWitness() = default;

private:
    static void fillPlayerBlob(DevPlayerBlob& blob, PlayerObject* src);
    static void setCategory(
        DevLiveWitness& w,
        DevWitnessCategory c,
        DevWitnessStatus status,
        uint64_t digest = 0
    );
    static uint64_t hashHazardNeighborhood(
        PlayLayer* layer, PlayerObject* player, float radiusX
    );
};

#endif // DEV_LIVE_WITNESS_HPP
