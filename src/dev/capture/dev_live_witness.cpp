#include "dev_live_witness.hpp"
#include "../../core/level/section_object_walk.hpp"
#include "../../core/sim/shadow_simulator.hpp"
#include "../../core/trigger_world/trigger_world_baker.hpp"
#include "../../core/trigger_world/trigger_world_store.hpp"
#include "../dev_config.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <vector>

uint64_t EbDevLiveWitnessImplementation::fnv1aMix(uint64_t hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

uint64_t EbDevLiveWitnessImplementation::fnv1aBytes(void const* data, size_t count) {
    uint64_t height = 14695981039346656037ull;
    auto const* valuePointer = static_cast<unsigned char const*>(data);
    for (size_t index = 0; index < count; ++index) {
        height = EbDevLiveWitnessImplementation::fnv1aMix(height, valuePointer[index]);
    }
    return height;
}

uint64_t EbDevLiveWitnessImplementation::fnv1aStr(char const* state) {
    if (!(!state)) {
        return EbDevLiveWitnessImplementation::fnv1aBytes(state, std::strlen(state));
    }
    return EbDevLiveWitnessImplementation::fnv1aBytes("", 0);
}

char const* EbDevLiveWitness::categoryId(DevWitnessCategory candidate) {
    switch (candidate) {
    case DevWitnessCategory::Player:
        return "player";
    case DevWitnessCategory::Dual:
        return "dual";
    case DevWitnessCategory::Clock:
        return "clock";
    case DevWitnessCategory::Orbs:
        return "orbs";
    case DevWitnessCategory::Triggers:
        return "triggers";
    case DevWitnessCategory::Movers:
        return "movers";
    case DevWitnessCategory::CollisionFlags:
        return "collisionFlags";
    }
    return "player";
}

bool EbDevLiveWitness::parseCategory(std::string const& s, DevWitnessCategory& out) {
    if (s == "player") {
        out = DevWitnessCategory::Player;
        return true;
    }
    if (s == "dual") {
        out = DevWitnessCategory::Dual;
        return true;
    }
    if (s == "clock") {
        out = DevWitnessCategory::Clock;
        return true;
    }
    if (s == "orbs") {
        out = DevWitnessCategory::Orbs;
        return true;
    }
    if (s == "triggers") {
        out = DevWitnessCategory::Triggers;
        return true;
    }
    if (s == "movers") {
        out = DevWitnessCategory::Movers;
        return true;
    }
    if (s == "collisionFlags") {
        out = DevWitnessCategory::CollisionFlags;
        return true;
    }
    return false;
}

char const* EbDevLiveWitness::statusId(DevWitnessStatus state) {
    switch (state) {
    case DevWitnessStatus::Absent:
        return "absent";
    case DevWitnessStatus::DigestOnly:
        return "digestOnly";
    case DevWitnessStatus::Partial:
        return "partial";
    case DevWitnessStatus::Restored:
        return "restored";
    }
    return "absent";
}

bool EbDevLiveWitness::parseStatus(std::string const& s, DevWitnessStatus& out) {
    if (s == "absent") {
        out = DevWitnessStatus::Absent;
        return true;
    }
    if (s == "digestOnly") {
        out = DevWitnessStatus::DigestOnly;
        return true;
    }
    if (s == "partial") {
        out = DevWitnessStatus::Partial;
        return true;
    }
    if (s == "restored") {
        out = DevWitnessStatus::Restored;
        return true;
    }
    return false;
}

char const* const* EbDevLiveWitness::playerBlobFieldNames(size_t& count) {
    static char const* const kNames[] = {
        "x",
        "y",
        "rotation",
        "scaleX",
        "scaleY",
        "yVelocity",
        "fallSpeed",
        "gravity",
        "gravityMod",
        "speedMultiplier",
        "playerSpeed",
        "vehicleSize",
        "platformerXVelocity",
        "isUpsideDown",
        "isSideways",
        "isGoingLeft",
        "isShip",
        "isBall",
        "isBird",
        "isSwing",
        "isDart",
        "isRobot",
        "isSpider",
        "isSecondPlayer",
        "isPlatformer",
        "isDashing",
        "isOnGround",
        "isOnGround2",
        "isOnSlope",
        "wasOnSlope",
        "jumpBuffered",
        "wasJumpBuffered",
        "stateRingJump",
        "hasEverJumped",
        "hasEverHitRing",
        "isLocked",
        "controlsDisabled",
        "maybeIsFalling",
        "maybeIsBoosted",
        "jumpHeld",
    };
    count = sizeof(kNames) / sizeof(kNames[0]);
    return kNames;
}

uint64_t EbDevLiveWitness::computePlayerBlobSchema() {
    size_t count = 0;
    char const* const* names = EbDevLiveWitness::playerBlobFieldNames(count);
    uint64_t height = 14695981039346656037ull;
    height = EbDevLiveWitnessImplementation::fnv1aMix(
        height, static_cast<uint64_t>(kPlayerBlobSchemaVersion));
    height = EbDevLiveWitnessImplementation::fnv1aMix(height, static_cast<uint64_t>(count));
    for (size_t index = 0; index < count; ++index) {
        height = EbDevLiveWitnessImplementation::fnv1aMix(
            height, EbDevLiveWitnessImplementation::fnv1aStr(names[index]));
    }
    return height;
}

void EbDevLiveWitness::setCategory(EbDevLiveWitness& w, DevWitnessCategory candidate,
                                 DevWitnessStatus status, uint64_t digest) {
    for (auto& e : w.categories) {
        if (e.category == candidate) {
            e.status = status;
            e.digest = digest;
            return;
        }
    }
    EbDevWitnessCategoryEntry e;
    e.category = candidate;
    e.status = status;
    e.digest = digest;
    w.categories.push_back(e);
}

DevWitnessStatus EbDevLiveWitness::categoryStatus(EbDevLiveWitness const& w,
                                                DevWitnessCategory candidate) {
    for (auto const& e : w.categories) {
        if (e.category == candidate)
            return e.status;
    }
    return DevWitnessStatus::Absent;
}

bool EbDevLiveWitness::hasWorldDep(EbDevLiveWitness const& w, DevWitnessCategory candidate) {
    return w.worldDeps.count(static_cast<int>(candidate)) > 0;
}

void EbDevLiveWitness::fillWorldDeps(EbDevLiveWitness& w, EbSimPlan const& plan, bool dualMode) {
    w.worldDeps.clear();
    // player + clock always.
    w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Player));
    w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Clock));
    w.worldDeps.insert(static_cast<int>(DevWitnessCategory::CollisionFlags));

    // AP9: dual mode tags Dual even when P2 pointer is missing (honest Absent).
    if (dualMode) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
    }
    if (plan.usedOrbWindow || plan.requireOrb || plan.foresightOrbPad) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Orbs));
    }
    // Triggers dep only when store has ready tracks that eval can spool.
    auto& tw = EbTriggerWorldStore::get();
    if (tw.loaded() && tw.hasTracks() && tw.readyTrackCount() > 0) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Triggers));
    }
    // Movers: only when freeze-world is on (poses) or we at least have a digest.
    if (w.freezeWorld || w.hasHazardDigest) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
    }
}

void EbDevLiveWitness::fillPlayerBlob(EbDevPlayerBlob& blob, PlayerObject* source) {
    blob = {};
    if (!source)
        return;
    blob.present = true;
    blob.x = source->getPositionX();
    blob.y = source->getPositionY();
    blob.rotation = source->getRotation();
    blob.scaleX = source->getScaleX();
    blob.scaleY = source->getScaleY();
    blob.yVelocity = source->m_yVelocity;
    blob.fallSpeed = source->m_fallSpeed;
    blob.gravity = source->m_gravity;
    blob.gravityMod = source->m_gravityMod;
    blob.speedMultiplier = source->m_speedMultiplier;
    blob.playerSpeed = source->m_playerSpeed;
    blob.vehicleSize = source->m_vehicleSize;
    blob.platformerXVelocity = source->m_platformerXVelocity;
    blob.isUpsideDown = source->m_isUpsideDown;
    blob.isSideways = source->m_isSideways;
    blob.isGoingLeft = source->m_isGoingLeft;
    blob.isShip = source->m_isShip;
    blob.isBall = source->m_isBall;
    blob.isBird = source->m_isBird;
    blob.isSwing = source->m_isSwing;
    blob.isDart = source->m_isDart;
    blob.isRobot = source->m_isRobot;
    blob.isSpider = source->m_isSpider;
    blob.isSecondPlayer = source->m_isSecondPlayer;
    blob.isPlatformer = source->m_isPlatformer;
    blob.isDashing = source->m_isDashing;
    blob.isOnGround = source->m_isOnGround;
    blob.isOnGround2 = source->m_isOnGround2;
    blob.isOnSlope = source->m_isOnSlope;
    blob.wasOnSlope = source->m_wasOnSlope;
    blob.jumpBuffered = source->m_jumpBuffered;
    blob.wasJumpBuffered = source->m_wasJumpBuffered;
    blob.stateRingJump = source->m_stateRingJump;
    blob.hasEverJumped = source->m_hasEverJumped;
    blob.hasEverHitRing = source->m_hasEverHitRing;
    blob.isLocked = source->m_isLocked;
    blob.controlsDisabled = source->m_controlsDisabled;
    blob.maybeIsFalling = source->m_maybeIsFalling;
    blob.maybeIsBoosted = source->m_maybeIsBoosted;
    {
        auto it = source->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        blob.jumpHeld = it != source->m_holdingButtons.end() && it->second;
    }
}

void EbDevLiveWitness::fillContactSnap(EbDevContactSnap& snap, PlayerObject* source) {
    snap = {};
    if (!source)
        return;
    snap.present = true;
    snap.maybeIsColliding = source->m_maybeIsColliding;
    snap.collidingWithLeft = source->m_collidingWithLeft;
    snap.collidingWithRight = source->m_collidingWithRight;
    snap.lastCollisionBottom = source->m_lastCollisionBottom;
    snap.lastCollisionTop = source->m_lastCollisionTop;
    snap.lastCollisionLeft = source->m_lastCollisionLeft;
    snap.lastCollisionRight = source->m_lastCollisionRight;
    snap.collidedTopMinY = source->m_collidedTopMinY;
    snap.collidedBottomMaxY = source->m_collidedBottomMaxY;
    snap.collidedLeftMaxX = source->m_collidedLeftMaxX;
    snap.collidedRightMinX = source->m_collidedRightMinX;
    snap.snapDistance = source->m_snapDistance;
    snap.groundYVelocity = source->m_groundYVelocity;
    snap.groundObjectMaterial = source->m_groundObjectMaterial;
    snap.collidedObjectId = source->m_collidedObject ? source->m_collidedObject->m_uniqueID : 0;
    snap.lastGroundObjectId =
        source->m_lastGroundObject ? source->m_lastGroundObject->m_uniqueID : 0;
    snap.objectSnappedToId = source->m_objectSnappedTo ? source->m_objectSnappedTo->m_uniqueID : 0;
    snap.isOnIce = source->m_isOnIce;
    snap.isSliding = source->m_isSliding;
}

bool EbDevLiveWitnessImplementation::hazardLikeObject(GameObject* object) {
    if (!object)
        return false;
    switch (object->m_objectType) {
    case GameObjectType::Hazard:
    case GameObjectType::AnimatedHazard:
        return true;
    default:
        return false;
    }
}

uint64_t EbDevLiveWitness::hashHazardNeighborhood(PlayLayer* layer, PlayerObject* player,
                                                float radiusX) {
    if (!layer || !player || !layer->m_objectLayer || radiusX <= 0.f)
        return 0;
    auto* children = layer->m_objectLayer->getChildren();
    if (!children)
        return 0;

    float const playerX = player->getPositionX();
    float const playerY = player->getPositionY();
    float const yReach = player->m_isShip || player->m_isDart || player->m_isSwing ? 120.f : 80.f;

    uint64_t height = 14695981039346656037ull;
    int count = 0;
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!EbDevLiveWitnessImplementation::hazardLikeObject(object))
            continue;
        float const objectX = object->getPositionX();
        float const objectY = object->getPositionY();
        if (objectX < playerX - 20.f || objectX > playerX + radiusX)
            continue;
        if (std::fabs(objectY - playerY) > yReach)
            continue;

        float half = object->getContentSize().width * std::fabs(object->getScaleX()) * 0.5f;
        if (half < 8.f)
            half = 8.f;
        height = EbDevLiveWitnessImplementation::fnv1aMix(height,
                                                        static_cast<uint64_t>(object->m_uniqueID));
        height = EbDevLiveWitnessImplementation::fnv1aMix(
            height, static_cast<uint64_t>(static_cast<int>(object->m_objectType)));
        long long const xUs = static_cast<long long>(std::llround(objectX * 100.0));
        long long const yUs = static_cast<long long>(std::llround(objectY * 100.0));
        long long const halfUs = static_cast<long long>(std::llround(half * 100.0));
        height = EbDevLiveWitnessImplementation::fnv1aMix(height, static_cast<uint64_t>(xUs));
        height = EbDevLiveWitnessImplementation::fnv1aMix(height, static_cast<uint64_t>(yUs));
        height = EbDevLiveWitnessImplementation::fnv1aMix(height, static_cast<uint64_t>(halfUs));
        ++count;
    }
    height = EbDevLiveWitnessImplementation::fnv1aMix(height, static_cast<uint64_t>(count));
    return height;
}

void EbDevLiveWitness::captureHazardSnaps(EbDevLiveWitness& w, PlayLayer* layer,
                                        PlayerObject* player, float radiusX) {
    w.hazards.clear();
    w.hasHazards = false;
    if (!layer || !player || radiusX <= 0.f)
        return;

    float const playerX = player->getPositionX();
    float const playerY = player->getPositionY();
    float const yReach = player->m_isShip || player->m_isDart || player->m_isSwing ? 120.f : 80.f;

    std::vector<EbDevHazardSnap> found;
    auto consider = [&](GameObject* object) {
        if (!EbDevLiveWitnessImplementation::hazardLikeObject(object) || object->m_uniqueID == 0)
            return;
        float const objectX = object->getPositionX();
        float const objectY = object->getPositionY();
        if (objectX < playerX - 20.f || objectX > playerX + radiusX)
            return;
        if (std::fabs(objectY - playerY) > yReach)
            return;
        auto const rect = object->getObjectRect();
        EbDevHazardSnap snap;
        snap.uniqueId = object->m_uniqueID;
        snap.objectType = static_cast<int>(object->m_objectType);
        snap.x = objectX;
        snap.y = objectY;
        snap.aabb.x = rect.origin.x;
        snap.aabb.y = rect.origin.y;
        snap.aabb.w = rect.size.width;
        snap.aabb.h = rect.size.height;
        float const dx = objectX - playerX;
        float const dy = objectY - playerY;
        snap.dist2 = dx * dx + dy * dy;
        found.push_back(snap);
    };

    if (auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr) {
        for (unsigned index = 0; index < children->count(); ++index) {
            consider(EbSectionObjectWalk::asGameObject(children->objectAtIndex(index)));
        }
    }
    EbSectionObjectWalk::forEachInSections(layer, consider);

    std::sort(found.begin(), found.end(), [](EbDevHazardSnap const& a, EbDevHazardSnap const& b) {
        if (a.dist2 != b.dist2)
            return a.dist2 < b.dist2;
        return a.uniqueId < b.uniqueId;
    });
    std::vector<EbDevHazardSnap> unique;
    unique.reserve(std::min(found.size(), static_cast<size_t>(kMaxHazardSnaps)));
    for (auto const& snap : found) {
        bool seen = false;
        for (auto const& existing : unique) {
            if (existing.uniqueId == snap.uniqueId) {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        unique.push_back(snap);
        if (static_cast<int>(unique.size()) >= kMaxHazardSnaps)
            break;
    }
    w.hazards = std::move(unique);
    w.hasHazards = true;
}

void EbDevLiveWitness::captureHitboxSnap(EbDevLiveWitness& w, PlayLayer* layer,
                                       PlayerObject* player) {
    w.hitbox = {};
    w.hasHitbox = false;
    if (!player)
        return;

    auto const live = player->getObjectRect();
    w.hitbox.present = true;
    w.hitbox.live.x = live.origin.x;
    w.hitbox.live.y = live.origin.y;
    w.hitbox.live.w = live.size.width;
    w.hitbox.live.h = live.size.height;

    auto& sim = EbShadowSimulator::get();
    PlayerObject* ghost = nullptr;
    if (layer && sim.isAttachedTo(layer))
        ghost = sim.shadow();
    if (ghost) {
        auto const ghostRect = ghost->getObjectRect();
        w.hitbox.hasGhost = true;
        w.hitbox.ghost.x = ghostRect.origin.x;
        w.hitbox.ghost.y = ghostRect.origin.y;
        w.hitbox.ghost.w = ghostRect.size.width;
        w.hitbox.ghost.h = ghostRect.size.height;
    }

    auto overlaps = [](EbDevAabb const& a, EbDevAabb const& b) -> bool {
        return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    };
    for (auto const& hazard : w.hazards) {
        if (overlaps(w.hitbox.live, hazard.aabb))
            ++w.hitbox.hazardOverlapLive;
        if (w.hitbox.hasGhost && overlaps(w.hitbox.ghost, hazard.aabb))
            ++w.hitbox.hazardOverlapGhost;
    }
    w.hasHitbox = true;
}

EbDevLiveWitness EbDevLiveWitness::capture(PlayLayer* layer, PlayerObject* player,
                                       PlayerObject* player2, EbSimPlan const& plan,
                                       EbDevClockContract::Stamp const& clock, int effectTick,
                                       bool freezeWorld) {
    EbDevLiveWitness w;
    w.freezeWorld = freezeWorld;
    w.playerBlobSchema = EbDevLiveWitness::computePlayerBlobSchema();
    w.clock = clock;
    w.clock.effectTick = effectTick;

    // --- player ---
    fillPlayerBlob(w.player, player);
    w.contact = {};
    w.hasContact = false;
    if (w.player.present) {
        setCategory(w, DevWitnessCategory::Player, DevWitnessStatus::Restored);
        // Bool/ground flags live in the blob; pointer refs → uniqueId contact snap (AP9).
        fillContactSnap(w.contact, player);
        w.hasContact = w.contact.present;
        setCategory(w, DevWitnessCategory::CollisionFlags, DevWitnessStatus::Partial);
    } else {
        setCategory(w, DevWitnessCategory::Player, DevWitnessStatus::Absent);
        setCategory(w, DevWitnessCategory::CollisionFlags, DevWitnessStatus::Absent);
    }

    // --- dual (AP9: dual mode without P2 → worldDep + Absent, never silent) ---
    bool const dualMode = layer && layer->m_gameState.m_isDualMode;
    w.dualContact = {};
    w.hasDualContact = false;
    if (dualMode && player2) {
        fillPlayerBlob(w.dual, player2);
        setCategory(w, DevWitnessCategory::Dual,
                    w.dual.present ? DevWitnessStatus::Restored : DevWitnessStatus::Absent);
        if (w.dual.present) {
            fillContactSnap(w.dualContact, player2);
            w.hasDualContact = w.dualContact.present;
        }
    } else {
        w.dual = {};
        // dualMode without P2: Absent + still tagged in worldDeps (below).
        setCategory(w, DevWitnessCategory::Dual, DevWitnessStatus::Absent);
    }

    // --- clock ---
    if (clock.valid && EbDevClockContract::get().clockOk(w.clock)) {
        setCategory(w, DevWitnessCategory::Clock, DevWitnessStatus::Restored);
    } else if (clock.valid) {
        setCategory(w, DevWitnessCategory::Clock, DevWitnessStatus::Partial);
    } else {
        setCategory(w, DevWitnessCategory::Clock, DevWitnessStatus::Absent);
    }

    // --- orbs: activated object IDs from GJGameState map ---
    w.orbIds.clear();
    if (layer) {
        auto const& ids = layer->m_gameState.m_activatedObjectIDs;
        w.orbIds.reserve(ids.size());
        for (auto const& kv : ids) {
            // Key is typically (uniqueId, …); keep unique first components.
            int const uid = kv.first.first;
            if (uid == 0)
                continue;
            bool seen = false;
            for (int existing : w.orbIds) {
                if (existing == uid) {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                w.orbIds.push_back(uid);
        }
    }
    bool const wantOrbs = plan.usedOrbWindow || plan.requireOrb || plan.foresightOrbPad;
    if (wantOrbs) {
        setCategory(w, DevWitnessCategory::Orbs,
                    w.orbIds.empty() ? DevWitnessStatus::Partial : DevWitnessStatus::Restored);
    } else if (!w.orbIds.empty()) {
        setCategory(w, DevWitnessCategory::Orbs, DevWitnessStatus::Partial);
    } else {
        setCategory(w, DevWitnessCategory::Orbs, DevWitnessStatus::Absent);
    }

    // --- triggers ---
    {
        auto& tw = EbTriggerWorldStore::get();
        auto& baker = EbTriggerWorldBaker::get();
        w.triggerStoreKey = tw.data().levelKey;
        w.triggerStoreVersion = tw.data().version;
        w.triggerApplyTick = effectTick;
        w.triggerTrackCount = static_cast<int>(tw.trackCount());
        w.triggerReadyTracks = static_cast<int>(tw.readyTrackCount());
        w.triggerBakeEffects = static_cast<int>(baker.lastBakedEffects());
        w.triggerBakeChildren = static_cast<int>(baker.lastChildCount());
        w.triggerCollectedEffects = static_cast<int>(baker.lastEffectCount());
        w.triggerSkipFollow = static_cast<int>(baker.lastSkippedFollow());
        w.triggerSkipDynamic = static_cast<int>(baker.lastSkippedDynamic());
        w.triggerSkipNonSpawn = static_cast<int>(baker.lastSkippedNonSpawn());
        w.triggerSkipTouch = static_cast<int>(baker.lastSkippedTouch());
        w.triggerCanSpool = tw.loaded() && tw.hasTracks() && tw.readyTrackCount() > 0;
        if (w.triggerCanSpool) {
            setCategory(w, DevWitnessCategory::Triggers, DevWitnessStatus::Partial);
        } else if (EbShadowSimulator::get().triggerWorldAppliedThisTick()) {
            setCategory(w, DevWitnessCategory::Triggers, DevWitnessStatus::Partial);
        } else {
            setCategory(w, DevWitnessCategory::Triggers, DevWitnessStatus::Absent);
        }
    }

    // --- movers / hazard digest ---
    // Hashes are ALWAYS DigestOnly. Poses (freeze-world) can be Restored.
    // AP6: optional Top-N hazard AABB + plan-time hitbox when recordHazards.
    w.moverPoses.clear();
    w.hazards.clear();
    w.hasHazards = false;
    w.hasHitbox = false;
    w.hasThreatZone = plan.threatZone.valid;
    w.threatZoneStartX = plan.threatZone.startX;
    w.threatZoneEndX = plan.threatZone.endX;
    if (player && layer) {
        float const radius = 160.f;
        w.hazardDigest = hashHazardNeighborhood(layer, player, radius);
        w.hasHazardDigest = true;
        if (EbDevConfig::recordHazards()) {
            captureHazardSnaps(w, layer, player, radius);
            captureHitboxSnap(w, layer, player);
        }

        if (freezeWorld) {
            auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
            if (children) {
                float const playerX = player->getPositionX();
                float const playerY = player->getPositionY();
                float const yReach =
                    player->m_isShip || player->m_isDart || player->m_isSwing ? 120.f : 80.f;
                auto& tw = EbTriggerWorldStore::get();
                for (unsigned index = 0; index < children->count(); ++index) {
                    auto* object =
                        geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
                    if (!object || object->m_uniqueID == 0)
                        continue;
                    float const objectX = object->getPositionX();
                    float const objectY = object->getPositionY();
                    if (objectX < playerX - 20.f || objectX > playerX + radius)
                        continue;
                    if (std::fabs(objectY - playerY) > yReach)
                        continue;

                    bool want = EbDevLiveWitnessImplementation::hazardLikeObject(object);
                    if (!want && tw.loaded()) {
                        auto const* track = tw.findTrack(object->m_uniqueID);
                        want = track && track->ready && !track->keys.empty();
                    }
                    if (!want)
                        continue;

                    EbDevObjectPose pose;
                    pose.uniqueId = object->m_uniqueID;
                    pose.x = objectX;
                    pose.y = objectY;
                    pose.rotation = object->getRotation();
                    pose.scaleX = object->getScaleX();
                    pose.scaleY = object->getScaleY();
                    pose.active = object->isVisible();
                    w.moverPoses.push_back(pose);
                }
            }
            // Vacuous empty set still Restored: freeze path held, nothing to write.
            setCategory(w, DevWitnessCategory::Movers, DevWitnessStatus::Restored, w.hazardDigest);
        } else {
            setCategory(w, DevWitnessCategory::Movers, DevWitnessStatus::DigestOnly,
                        w.hazardDigest);
        }
    } else {
        setCategory(w, DevWitnessCategory::Movers, DevWitnessStatus::Absent);
    }

    fillWorldDeps(w, plan, dualMode);
    w.valid = true;
    return w;
}

bool EbDevLiveWitness::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    expect(std::strcmp(categoryId(DevWitnessCategory::Player), "player") == 0, "cat-player");
    expect(std::strcmp(statusId(DevWitnessStatus::DigestOnly), "digestOnly") == 0, "status-digest");

    uint64_t const s1 = computePlayerBlobSchema();
    uint64_t const s2 = computePlayerBlobSchema();
    expect(s1 == s2 && s1 != 0, "schema-stable");

    size_t count = 0;
    char const* const* names = playerBlobFieldNames(count);
    expect(count >= 20, "schema-fields");
    expect(names && names[0] && std::strcmp(names[0], "x") == 0, "schema-x");

    // Synthetic witness: missing player → player Absent; hazard digestOnly.
    {
        EbSimPlan plan;
        plan.requireOrb = true;
        EbDevClockContract::Stamp clock = EbDevClockContract::get().makeStamp(
            10, false, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f, DevSimPathId::Plan);
        EbDevLiveWitness width = capture(nullptr, nullptr, nullptr, plan, clock, 11, false);
        expect(width.valid, "cap-valid");
        expect(categoryStatus(width, DevWitnessCategory::Player) == DevWitnessStatus::Absent,
               "cap-player-absent");
        expect(categoryStatus(width, DevWitnessCategory::Clock) == DevWitnessStatus::Restored,
               "cap-clock-ok");
        expect(categoryStatus(width, DevWitnessCategory::Movers) == DevWitnessStatus::Absent,
               "cap-movers-no-layer");
        expect(hasWorldDep(width, DevWitnessCategory::Player), "dep-player");
        expect(hasWorldDep(width, DevWitnessCategory::Clock), "dep-clock");
        expect(hasWorldDep(width, DevWitnessCategory::Orbs), "dep-orbs-require");
        expect(!hasWorldDep(width, DevWitnessCategory::Dual), "dep-no-dual");
    }

    // worldDeps: foresight orb pad tags orbs; dualMode tags dual even without P2.
    {
        EbDevLiveWitness width;
        width.freezeWorld = false;
        width.hasHazardDigest = true;
        EbSimPlan plan;
        plan.foresightOrbPad = true;
        fillWorldDeps(width, plan, true);
        expect(hasWorldDep(width, DevWitnessCategory::Orbs), "dep-foresight");
        expect(hasWorldDep(width, DevWitnessCategory::Dual), "dep-dual");
        expect(hasWorldDep(width, DevWitnessCategory::Movers), "dep-movers-digest");
        expect(hasWorldDep(width, DevWitnessCategory::CollisionFlags), "dep-flags");
    }

    // Hashes alone stay DigestOnly; freeze-world poses may be Restored.
    {
        EbDevLiveWitness width;
        setCategory(width, DevWitnessCategory::Movers, DevWitnessStatus::DigestOnly, 0xabc);
        expect(categoryStatus(width, DevWitnessCategory::Movers) == DevWitnessStatus::DigestOnly,
               "movers-digest-only");
        expect(std::strcmp(statusId(DevWitnessStatus::Restored), "restored") == 0,
               "status-restored-id");

        EbDevLiveWitness wf;
        wf.freezeWorld = true;
        wf.hasHazardDigest = true;
        wf.hazardDigest = 0xabc;
        EbDevObjectPose valuePointer{};
        valuePointer.uniqueId = 9;
        valuePointer.x = 3.f;
        wf.moverPoses.push_back(valuePointer);
        setCategory(wf, DevWitnessCategory::Movers, DevWitnessStatus::Restored, 0xabc);
        expect(categoryStatus(wf, DevWitnessCategory::Movers) == DevWitnessStatus::Restored,
               "movers-poses-restored");
        expect(wf.moverPoses.size() == 1, "movers-poses-n");
    }

    DevWitnessCategory parsed = DevWitnessCategory::Player;
    expect(parseCategory("triggers", parsed) && parsed == DevWitnessCategory::Triggers,
           "parse-cat");
    DevWitnessStatus state = DevWitnessStatus::Absent;
    expect(parseStatus("digestOnly", state) && state == DevWitnessStatus::DigestOnly,
           "parse-status");

    if (fails == 0) {
        report = fmt::format("DevLiveWitness selfCheck OK schema={:016x}", s1);
        return true;
    }
    report = std::string("DevLiveWitness selfCheck fails: ") + report;
    return false;
}
