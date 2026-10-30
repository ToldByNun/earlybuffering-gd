#include "DevLiveWitness.hpp"

#include "../core/ShadowSimulator.hpp"
#include "../core/TriggerWorldBaker.hpp"
#include "../core/TriggerWorldStore.hpp"

#include <Geode/Geode.hpp>
#include <cmath>
#include <cstring>
#include <fmt/format.h>

using namespace geode::prelude;

static uint64_t fnv1aMix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
    return h;
}

static uint64_t fnv1aBytes(void const* data, size_t n) {
    uint64_t h = 14695981039346656037ull;
    auto const* p = static_cast<unsigned char const*>(data);
    for (size_t i = 0; i < n; ++i) {
        h = fnv1aMix(h, p[i]);
    }
    return h;
}

static uint64_t fnv1aStr(char const* s) {
    if (!s) return fnv1aBytes("", 0);
    return fnv1aBytes(s, std::strlen(s));
}

char const* DevLiveWitness::categoryId(DevWitnessCategory c) {
    switch (c) {
        case DevWitnessCategory::Player: return "player";
        case DevWitnessCategory::Dual: return "dual";
        case DevWitnessCategory::Clock: return "clock";
        case DevWitnessCategory::Orbs: return "orbs";
        case DevWitnessCategory::Triggers: return "triggers";
        case DevWitnessCategory::Movers: return "movers";
        case DevWitnessCategory::CollisionFlags: return "collisionFlags";
    }
    return "player";
}

bool DevLiveWitness::parseCategory(std::string const& s, DevWitnessCategory& out) {
    if (s == "player") { out = DevWitnessCategory::Player; return true; }
    if (s == "dual") { out = DevWitnessCategory::Dual; return true; }
    if (s == "clock") { out = DevWitnessCategory::Clock; return true; }
    if (s == "orbs") { out = DevWitnessCategory::Orbs; return true; }
    if (s == "triggers") { out = DevWitnessCategory::Triggers; return true; }
    if (s == "movers") { out = DevWitnessCategory::Movers; return true; }
    if (s == "collisionFlags") { out = DevWitnessCategory::CollisionFlags; return true; }
    return false;
}

char const* DevLiveWitness::statusId(DevWitnessStatus s) {
    switch (s) {
        case DevWitnessStatus::Absent: return "absent";
        case DevWitnessStatus::DigestOnly: return "digestOnly";
        case DevWitnessStatus::Partial: return "partial";
        case DevWitnessStatus::Restored: return "restored";
    }
    return "absent";
}

bool DevLiveWitness::parseStatus(std::string const& s, DevWitnessStatus& out) {
    if (s == "absent") { out = DevWitnessStatus::Absent; return true; }
    if (s == "digestOnly") { out = DevWitnessStatus::DigestOnly; return true; }
    if (s == "partial") { out = DevWitnessStatus::Partial; return true; }
    if (s == "restored") { out = DevWitnessStatus::Restored; return true; }
    return false;
}

char const* const* DevLiveWitness::playerBlobFieldNames(size_t& count) {
    static char const* const kNames[] = {
        "x", "y", "rotation", "scaleX", "scaleY",
        "yVelocity", "fallSpeed", "gravity", "gravityMod", "speedMultiplier",
        "playerSpeed", "vehicleSize", "platformerXVelocity",
        "isUpsideDown", "isSideways", "isGoingLeft",
        "isShip", "isBall", "isBird", "isSwing", "isDart", "isRobot", "isSpider",
        "isSecondPlayer", "isPlatformer", "isDashing",
        "isOnGround", "isOnGround2", "isOnSlope", "wasOnSlope",
        "jumpBuffered", "wasJumpBuffered", "stateRingJump",
        "hasEverJumped", "hasEverHitRing", "isLocked", "controlsDisabled",
        "maybeIsFalling", "maybeIsBoosted",
    };
    count = sizeof(kNames) / sizeof(kNames[0]);
    return kNames;
}

uint64_t DevLiveWitness::computePlayerBlobSchema() {
    size_t n = 0;
    char const* const* names = DevLiveWitness::playerBlobFieldNames(n);
    uint64_t h = 14695981039346656037ull;
    h = fnv1aMix(h, static_cast<uint64_t>(kPlayerBlobSchemaVersion));
    h = fnv1aMix(h, static_cast<uint64_t>(n));
    for (size_t i = 0; i < n; ++i) {
        h = fnv1aMix(h, fnv1aStr(names[i]));
    }
    return h;
}

void DevLiveWitness::setCategory(
    DevLiveWitness& w,
    DevWitnessCategory c,
    DevWitnessStatus status,
    uint64_t digest
) {
    for (auto& e : w.categories) {
        if (e.category == c) {
            e.status = status;
            e.digest = digest;
            return;
        }
    }
    DevWitnessCategoryEntry e;
    e.category = c;
    e.status = status;
    e.digest = digest;
    w.categories.push_back(e);
}

DevWitnessStatus DevLiveWitness::categoryStatus(
    DevLiveWitness const& w, DevWitnessCategory c
) {
    for (auto const& e : w.categories) {
        if (e.category == c) return e.status;
    }
    return DevWitnessStatus::Absent;
}

bool DevLiveWitness::hasWorldDep(DevLiveWitness const& w, DevWitnessCategory c) {
    return w.worldDeps.count(static_cast<int>(c)) > 0;
}

void DevLiveWitness::fillWorldDeps(
    DevLiveWitness& w, SimPlan const& plan, bool dualActive
) {
    w.worldDeps.clear();
    // player + clock always.
    w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Player));
    w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Clock));
    w.worldDeps.insert(static_cast<int>(DevWitnessCategory::CollisionFlags));

    if (dualActive) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Dual));
    }
    if (plan.usedOrbWindow || plan.requireOrb || plan.foresightOrbPad) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Orbs));
    }
    // Triggers dep when store ready or sim already applied this tick.
    auto& tw = TriggerWorldStore::get();
    if (tw.loaded() && tw.hasTracks() && tw.readyTrackCount() > 0) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Triggers));
    }
    if (ShadowSimulator::get().triggerWorldAppliedThisTick()) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Triggers));
    }
    // Movers: only when freeze-world is on (poses) or we at least have a digest.
    if (w.freezeWorld || w.hasHazardDigest) {
        w.worldDeps.insert(static_cast<int>(DevWitnessCategory::Movers));
    }
}

void DevLiveWitness::fillPlayerBlob(DevPlayerBlob& blob, PlayerObject* src) {
    blob = {};
    if (!src) return;
    blob.present = true;
    blob.x = src->getPositionX();
    blob.y = src->getPositionY();
    blob.rotation = src->getRotation();
    blob.scaleX = src->getScaleX();
    blob.scaleY = src->getScaleY();
    blob.yVelocity = src->m_yVelocity;
    blob.fallSpeed = src->m_fallSpeed;
    blob.gravity = src->m_gravity;
    blob.gravityMod = src->m_gravityMod;
    blob.speedMultiplier = src->m_speedMultiplier;
    blob.playerSpeed = src->m_playerSpeed;
    blob.vehicleSize = src->m_vehicleSize;
    blob.platformerXVelocity = src->m_platformerXVelocity;
    blob.isUpsideDown = src->m_isUpsideDown;
    blob.isSideways = src->m_isSideways;
    blob.isGoingLeft = src->m_isGoingLeft;
    blob.isShip = src->m_isShip;
    blob.isBall = src->m_isBall;
    blob.isBird = src->m_isBird;
    blob.isSwing = src->m_isSwing;
    blob.isDart = src->m_isDart;
    blob.isRobot = src->m_isRobot;
    blob.isSpider = src->m_isSpider;
    blob.isSecondPlayer = src->m_isSecondPlayer;
    blob.isPlatformer = src->m_isPlatformer;
    blob.isDashing = src->m_isDashing;
    blob.isOnGround = src->m_isOnGround;
    blob.isOnGround2 = src->m_isOnGround2;
    blob.isOnSlope = src->m_isOnSlope;
    blob.wasOnSlope = src->m_wasOnSlope;
    blob.jumpBuffered = src->m_jumpBuffered;
    blob.wasJumpBuffered = src->m_wasJumpBuffered;
    blob.stateRingJump = src->m_stateRingJump;
    blob.hasEverJumped = src->m_hasEverJumped;
    blob.hasEverHitRing = src->m_hasEverHitRing;
    blob.isLocked = src->m_isLocked;
    blob.controlsDisabled = src->m_controlsDisabled;
    blob.maybeIsFalling = src->m_maybeIsFalling;
    blob.maybeIsBoosted = src->m_maybeIsBoosted;
}

static bool hazardLikeObject(GameObject* obj) {
    if (!obj) return false;
    switch (obj->m_objectType) {
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:
            return true;
        default:
            return false;
    }
}

uint64_t DevLiveWitness::hashHazardNeighborhood(
    PlayLayer* layer, PlayerObject* player, float radiusX
) {
    if (!layer || !player || !layer->m_objectLayer || radiusX <= 0.f) return 0;
    auto* children = layer->m_objectLayer->getChildren();
    if (!children) return 0;

    float const px = player->getPositionX();
    float const py = player->getPositionY();
    float const yReach = player->m_isShip || player->m_isDart || player->m_isSwing
        ? 120.f
        : 80.f;

    uint64_t h = 14695981039346656037ull;
    int count = 0;
    for (unsigned i = 0; i < children->count(); ++i) {
        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!hazardLikeObject(obj)) continue;
        float const ox = obj->getPositionX();
        float const oy = obj->getPositionY();
        if (ox < px - 20.f || ox > px + radiusX) continue;
        if (std::fabs(oy - py) > yReach) continue;

        float half = obj->getContentSize().width * std::fabs(obj->getScaleX()) * 0.5f;
        if (half < 8.f) half = 8.f;
        h = fnv1aMix(h, static_cast<uint64_t>(obj->m_uniqueID));
        h = fnv1aMix(h, static_cast<uint64_t>(static_cast<int>(obj->m_objectType)));
        long long const xUs = static_cast<long long>(std::llround(ox * 100.0));
        long long const yUs = static_cast<long long>(std::llround(oy * 100.0));
        long long const halfUs = static_cast<long long>(std::llround(half * 100.0));
        h = fnv1aMix(h, static_cast<uint64_t>(xUs));
        h = fnv1aMix(h, static_cast<uint64_t>(yUs));
        h = fnv1aMix(h, static_cast<uint64_t>(halfUs));
        ++count;
    }
    h = fnv1aMix(h, static_cast<uint64_t>(count));
    return h;
}

DevLiveWitness DevLiveWitness::capture(
    PlayLayer* layer,
    PlayerObject* player,
    PlayerObject* player2,
    SimPlan const& plan,
    DevClockContract::Stamp const& clock,
    int effectTick,
    bool freezeWorld
) {
    DevLiveWitness w;
    w.freezeWorld = freezeWorld;
    w.playerBlobSchema = DevLiveWitness::computePlayerBlobSchema();
    w.clock = clock;
    w.clock.effectTick = effectTick;

    // --- player ---
    fillPlayerBlob(w.player, player);
    if (w.player.present) {
        setCategory(w, DevWitnessCategory::Player, DevWitnessStatus::Restored);
        // Copied bool/ground flags are in the blob; pointer collision refs absent.
        setCategory(
            w, DevWitnessCategory::CollisionFlags, DevWitnessStatus::Partial
        );
    }
    else {
        setCategory(w, DevWitnessCategory::Player, DevWitnessStatus::Absent);
        setCategory(
            w, DevWitnessCategory::CollisionFlags, DevWitnessStatus::Absent
        );
    }

    // --- dual ---
    bool const dualActive = player2 != nullptr
        && layer
        && layer->m_gameState.m_isDualMode;
    if (dualActive && player2) {
        fillPlayerBlob(w.dual, player2);
        setCategory(
            w,
            DevWitnessCategory::Dual,
            w.dual.present ? DevWitnessStatus::Restored : DevWitnessStatus::Absent
        );
    }
    else {
        setCategory(w, DevWitnessCategory::Dual, DevWitnessStatus::Absent);
    }

    // --- clock ---
    if (clock.valid && DevClockContract::get().clockOk(w.clock)) {
        setCategory(w, DevWitnessCategory::Clock, DevWitnessStatus::Restored);
    }
    else if (clock.valid) {
        setCategory(w, DevWitnessCategory::Clock, DevWitnessStatus::Partial);
    }
    else {
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
            if (uid == 0) continue;
            bool seen = false;
            for (int existing : w.orbIds) {
                if (existing == uid) { seen = true; break; }
            }
            if (!seen) w.orbIds.push_back(uid);
        }
    }
    bool const wantOrbs = plan.usedOrbWindow || plan.requireOrb || plan.foresightOrbPad;
    if (wantOrbs) {
        setCategory(
            w,
            DevWitnessCategory::Orbs,
            w.orbIds.empty() ? DevWitnessStatus::Partial : DevWitnessStatus::Restored
        );
    }
    else if (!w.orbIds.empty()) {
        setCategory(w, DevWitnessCategory::Orbs, DevWitnessStatus::Partial);
    }
    else {
        setCategory(w, DevWitnessCategory::Orbs, DevWitnessStatus::Absent);
    }

    // --- triggers ---
    {
        auto& tw = TriggerWorldStore::get();
        auto& baker = TriggerWorldBaker::get();
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
        w.triggerCanSpool = tw.loaded()
            && tw.hasTracks()
            && tw.readyTrackCount() > 0;
        if (w.triggerCanSpool) {
            setCategory(
                w, DevWitnessCategory::Triggers, DevWitnessStatus::Partial
            );
        }
        else if (ShadowSimulator::get().triggerWorldAppliedThisTick()) {
            setCategory(
                w, DevWitnessCategory::Triggers, DevWitnessStatus::Partial
            );
        }
        else {
            setCategory(
                w, DevWitnessCategory::Triggers, DevWitnessStatus::Absent
            );
        }
    }

    // --- movers / hazard digest ---
    // Hashes are ALWAYS DigestOnly. Poses (freeze-world) can be Restored.
    w.moverPoses.clear();
    if (player && layer) {
        float const radius = 160.f;
        w.hazardDigest = hashHazardNeighborhood(layer, player, radius);
        w.hasHazardDigest = true;

        if (freezeWorld) {
            auto* children = layer->m_objectLayer
                ? layer->m_objectLayer->getChildren()
                : nullptr;
            if (children) {
                float const px = player->getPositionX();
                float const py = player->getPositionY();
                float const yReach =
                    player->m_isShip || player->m_isDart || player->m_isSwing
                        ? 120.f
                        : 80.f;
                auto& tw = TriggerWorldStore::get();
                for (unsigned i = 0; i < children->count(); ++i) {
                    auto* obj = typeinfo_cast<GameObject*>(
                        children->objectAtIndex(i)
                    );
                    if (!obj || obj->m_uniqueID == 0) continue;
                    float const ox = obj->getPositionX();
                    float const oy = obj->getPositionY();
                    if (ox < px - 20.f || ox > px + radius) continue;
                    if (std::fabs(oy - py) > yReach) continue;

                    bool want = hazardLikeObject(obj);
                    if (!want && tw.loaded()) {
                        auto const* track = tw.findTrack(obj->m_uniqueID);
                        want = track && track->ready && !track->keys.empty();
                    }
                    if (!want) continue;

                    DevObjectPose pose;
                    pose.uniqueId = obj->m_uniqueID;
                    pose.x = ox;
                    pose.y = oy;
                    pose.rotation = obj->getRotation();
                    pose.scaleX = obj->getScaleX();
                    pose.scaleY = obj->getScaleY();
                    pose.active = obj->isVisible();
                    w.moverPoses.push_back(pose);
                }
            }
            // Vacuous empty set still Restored: freeze path held, nothing to write.
            setCategory(
                w,
                DevWitnessCategory::Movers,
                DevWitnessStatus::Restored,
                w.hazardDigest
            );
        }
        else {
            setCategory(
                w,
                DevWitnessCategory::Movers,
                DevWitnessStatus::DigestOnly,
                w.hazardDigest
            );
        }
    }
    else {
        setCategory(w, DevWitnessCategory::Movers, DevWitnessStatus::Absent);
    }

    fillWorldDeps(w, plan, dualActive);
    w.valid = true;
    return w;
}

bool DevLiveWitness::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    expect(
        std::strcmp(categoryId(DevWitnessCategory::Player), "player") == 0,
        "cat-player"
    );
    expect(
        std::strcmp(statusId(DevWitnessStatus::DigestOnly), "digestOnly") == 0,
        "status-digest"
    );

    uint64_t const s1 = computePlayerBlobSchema();
    uint64_t const s2 = computePlayerBlobSchema();
    expect(s1 == s2 && s1 != 0, "schema-stable");

    size_t n = 0;
    char const* const* names = playerBlobFieldNames(n);
    expect(n >= 20, "schema-fields");
    expect(names && names[0] && std::strcmp(names[0], "x") == 0, "schema-x");

    // Synthetic witness: missing player → player Absent; hazard digestOnly.
    {
        SimPlan plan;
        plan.requireOrb = true;
        DevClockContract::Stamp clock = DevClockContract::get().makeStamp(
            10, false, false, 1.f / 240.f, 1.f / 240.f, 1.f / 240.f, 1.f,
            DevSimPathId::Plan
        );
        DevLiveWitness w = capture(
            nullptr, nullptr, nullptr, plan, clock, 11, false
        );
        expect(w.valid, "cap-valid");
        expect(
            categoryStatus(w, DevWitnessCategory::Player)
                == DevWitnessStatus::Absent,
            "cap-player-absent"
        );
        expect(
            categoryStatus(w, DevWitnessCategory::Clock)
                == DevWitnessStatus::Restored,
            "cap-clock-ok"
        );
        expect(
            categoryStatus(w, DevWitnessCategory::Movers)
                == DevWitnessStatus::Absent,
            "cap-movers-no-layer"
        );
        expect(hasWorldDep(w, DevWitnessCategory::Player), "dep-player");
        expect(hasWorldDep(w, DevWitnessCategory::Clock), "dep-clock");
        expect(hasWorldDep(w, DevWitnessCategory::Orbs), "dep-orbs-require");
        expect(!hasWorldDep(w, DevWitnessCategory::Dual), "dep-no-dual");
    }

    // worldDeps: foresight orb pad tags orbs; dualActive tags dual.
    {
        DevLiveWitness w;
        w.freezeWorld = false;
        w.hasHazardDigest = true;
        SimPlan plan;
        plan.foresightOrbPad = true;
        fillWorldDeps(w, plan, true);
        expect(hasWorldDep(w, DevWitnessCategory::Orbs), "dep-foresight");
        expect(hasWorldDep(w, DevWitnessCategory::Dual), "dep-dual");
        expect(hasWorldDep(w, DevWitnessCategory::Movers), "dep-movers-digest");
    }

    // Hashes alone stay DigestOnly; freeze-world poses may be Restored.
    {
        DevLiveWitness w;
        setCategory(
            w, DevWitnessCategory::Movers, DevWitnessStatus::DigestOnly, 0xabc
        );
        expect(
            categoryStatus(w, DevWitnessCategory::Movers)
                == DevWitnessStatus::DigestOnly,
            "movers-digest-only"
        );
        expect(
            std::strcmp(statusId(DevWitnessStatus::Restored), "restored") == 0,
            "status-restored-id"
        );

        DevLiveWitness wf;
        wf.freezeWorld = true;
        wf.hasHazardDigest = true;
        wf.hazardDigest = 0xabc;
        DevObjectPose p{};
        p.uniqueId = 9;
        p.x = 3.f;
        wf.moverPoses.push_back(p);
        setCategory(
            wf, DevWitnessCategory::Movers, DevWitnessStatus::Restored, 0xabc
        );
        expect(
            categoryStatus(wf, DevWitnessCategory::Movers)
                == DevWitnessStatus::Restored,
            "movers-poses-restored"
        );
        expect(wf.moverPoses.size() == 1, "movers-poses-n");
    }

    DevWitnessCategory parsed = DevWitnessCategory::Player;
    expect(parseCategory("triggers", parsed)
            && parsed == DevWitnessCategory::Triggers,
        "parse-cat");
    DevWitnessStatus st = DevWitnessStatus::Absent;
    expect(parseStatus("digestOnly", st) && st == DevWitnessStatus::DigestOnly,
        "parse-status");

    if (fails == 0) {
        report = fmt::format(
            "DevLiveWitness selfCheck OK schema={:016x}", s1
        );
        return true;
    }
    report = std::string("DevLiveWitness selfCheck fails: ") + report;
    return false;
}
