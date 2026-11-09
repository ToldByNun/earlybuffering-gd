#include "trigger_classify.hpp"
#include <Geode/Geode.hpp>
#include <cmath>
#include <fmt/format.h>

bool EbTriggerClassify::isMoveTriggerId(int objectId) {
    return objectId == 901 || objectId == 1611;
}

bool EbTriggerClassify::isRotateTriggerId(int objectId) {
    return objectId == 1346;
}

bool EbTriggerClassify::isScaleTriggerId(int objectId) {
    return objectId == 2067;
}

bool EbTriggerClassify::isSpawnTriggerId(int objectId) {
    return objectId == 1268;
}

bool EbTriggerClassify::isToggleTriggerId(int objectId) {
    // Classic Toggle — enables/disables target group (solids/hazards on/off).
    return objectId == 1049;
}

bool EbTriggerClassify::isToggleOrbId(int objectId) {
    // Custom / toggle orb — player hit toggles target group (spawn/despawn).
    return objectId == 1594;
}

bool EbTriggerClassify::isFollowTriggerId(int objectId) {
    // 1347 Advanced Follow, 1585 Follow Player Y, 1817 Follow.
    return objectId == 1347 || objectId == 1585 || objectId == 1817;
}

bool EbTriggerClassify::isFollowPlayerTriggerId(int objectId) {
    // Always couples to the live player path.
    return objectId == 1585;
}

bool EbTriggerClassify::isNonLethalTriggerId(int objectId) {
    if (objectId == 1913 || objectId == 1914 || objectId == 1915 || objectId == 1916 ||
        objectId == 1917 || objectId == 2015 || objectId == 2016 || objectId == 2901) {
        return true;
    }
    switch (objectId) {
    case 29:
    case 1006:
    case 1007:
    // 1049 Toggle is collision-relevant — not listed here.
    case 1520:
    case 1818:
    case 3605:
    case 3606:
    case 3607:
    case 3612:
    case 3613:
        return true;
    default:
        return false;
    }
}

bool EbTriggerClassify::isLethalMotionCandidateId(int objectId) {
    return EbTriggerClassify::isMoveTriggerId(objectId) ||
           EbTriggerClassify::isRotateTriggerId(objectId) ||
           EbTriggerClassify::isScaleTriggerId(objectId) ||
           EbTriggerClassify::isSpawnTriggerId(objectId) ||
           EbTriggerClassify::isToggleTriggerId(objectId) ||
           EbTriggerClassify::isToggleOrbId(objectId) || EbTriggerClassify::isFollowTriggerId(objectId);
}

bool EbTriggerClassify::isStatic(TriggerKind kind) {
    return kind == TriggerKind::StaticMove || kind == TriggerKind::StaticRotate ||
           kind == TriggerKind::StaticScale || kind == TriggerKind::StaticSpawn ||
           kind == TriggerKind::StaticToggle;
}

bool EbTriggerClassify::isDynamic(TriggerKind kind) {
    return kind == TriggerKind::DynamicFollow || kind == TriggerKind::DynamicOther;
}

bool EbTriggerClassify::isMotion(TriggerKind kind) {
    return kind == TriggerKind::StaticMove || kind == TriggerKind::StaticRotate ||
           kind == TriggerKind::StaticScale || kind == TriggerKind::DynamicFollow ||
           kind == TriggerKind::DynamicOther;
}

char const* EbTriggerClassify::kindName(TriggerKind kind) {
    switch (kind) {
    case TriggerKind::Ignore:
        return "Ignore";
    case TriggerKind::StaticMove:
        return "StaticMove";
    case TriggerKind::StaticRotate:
        return "StaticRotate";
    case TriggerKind::StaticScale:
        return "StaticScale";
    case TriggerKind::StaticSpawn:
        return "StaticSpawn";
    case TriggerKind::StaticToggle:
        return "StaticToggle";
    case TriggerKind::DynamicFollow:
        return "DynamicFollow";
    case TriggerKind::DynamicOther:
        return "DynamicOther";
    default:
        return "?";
    }
}

bool EbTriggerClassify::dependsOnLivePlayer(MotionHints const& hints) {
    if (hints.lockToPlayerX || hints.lockToPlayerY)
        return true;
    if (hints.rotateFollowP1 || hints.rotateFollowP2)
        return true;
    if (hints.targetPlayer1 || hints.targetPlayer2)
        return true;
    if (EbTriggerClassify::isFollowPlayerTriggerId(hints.objectId))
        return true;
    // Camera lock on a mover: camera usually tracks the player → player-coupled.
    if (!(hints.lockToCameraX || hints.lockToCameraY)) {
        return false;
    }
    return true;
}

bool EbTriggerClassify::hasNonPlayerDynamicHints(MotionHints const& hints) {
    // Touch start time depends on when the player hits the trigger — not bakeable
    // as a fixed absolute timeline from attempt 0 without knowing touch tick.
    bool const collisionId = EbTriggerClassify::isMoveTriggerId(hints.objectId) ||
                             EbTriggerClassify::isRotateTriggerId(hints.objectId) ||
                             EbTriggerClassify::isScaleTriggerId(hints.objectId) ||
                             EbTriggerClassify::isFollowTriggerId(hints.objectId) ||
                             EbTriggerClassify::isToggleTriggerId(hints.objectId) ||
                             EbTriggerClassify::isSpawnTriggerId(hints.objectId);
    if (!(hints.touchTriggered && collisionId)) {
        return false;
    }
    return true;
}

TriggerKind EbTriggerClassify::classifyHints(MotionHints const& hints) {
    if (!hints.isEffect && hints.objectId == 0) {
        return TriggerKind::Ignore;
    }

    int const id = hints.objectId;

    if (EbTriggerClassify::isNonLethalTriggerId(id)) {
        return TriggerKind::Ignore;
    }
    if (!EbTriggerClassify::isLethalMotionCandidateId(id)) {
        return TriggerKind::Ignore;
    }

    // Toggle orb: on/off at player hit time — never a fixed absolute timeline.
    if (EbTriggerClassify::isToggleOrbId(id)) {
        return TriggerKind::DynamicOther;
    }

    // Player-coupled → cannot put a single static track in the cache.
    if (EbTriggerClassify::dependsOnLivePlayer(hints)) {
        return TriggerKind::DynamicFollow;
    }
    // Touch-activated motion (start time = player path).
    if (EbTriggerClassify::hasNonPlayerDynamicHints(hints)) {
        return TriggerKind::DynamicOther;
    }

    // Group-follow / move-target / "dynamic mode" toward another group:
    // still deterministic if the target group is itself static — StaticMove.
    // (Baker resolves the chain later; classifier only gates player coupling.)
    if (EbTriggerClassify::isFollowTriggerId(id) || hints.useMoveTarget || hints.followCpp ||
        hints.dynamicMode || hints.directionFollowSnap360) {
        if (EbTriggerClassify::isFollowPlayerTriggerId(id)) {
            return TriggerKind::DynamicFollow;
        }
        // Follow / move-target of another group ID → bakeable chain.
        return TriggerKind::StaticMove;
    }

    if (EbTriggerClassify::isMoveTriggerId(id)) {
        if (std::fabs(hints.moveOffsetX) < 1e-4f && std::fabs(hints.moveOffsetY) < 1e-4f &&
            id == 1611) {
            return TriggerKind::Ignore;
        }
        return TriggerKind::StaticMove;
    }
    if (EbTriggerClassify::isRotateTriggerId(id)) {
        return TriggerKind::StaticRotate;
    }
    if (EbTriggerClassify::isScaleTriggerId(id)) {
        return TriggerKind::StaticScale;
    }
    if (EbTriggerClassify::isSpawnTriggerId(id)) {
        return TriggerKind::StaticSpawn;
    }
    if (EbTriggerClassify::isToggleTriggerId(id)) {
        return TriggerKind::StaticToggle;
    }
    return TriggerKind::Ignore;
}

EbTriggerClassify::MotionHints EbTriggerClassify::hintsFromObject(GameObject* object) {
    MotionHints h;
    if (!object)
        return h;

    h.objectId = object->m_objectID;

    auto* effect = geode::prelude::typeinfo_cast<EffectGameObject*>(object);
    if (!effect) {
        h.isEffect = false;
        return h;
    }

    h.isEffect = true;
    h.lockToPlayerX = effect->m_lockToPlayerX;
    h.lockToPlayerY = effect->m_lockToPlayerY;
    h.lockToCameraX = effect->m_lockToCameraX;
    h.lockToCameraY = effect->m_lockToCameraY;
    h.followCpp = effect->m_followCPP;
    h.rotateFollowP1 = effect->m_rotateFollowP1;
    h.rotateFollowP2 = effect->m_rotateFollowP2;
    h.directionFollowSnap360 = effect->m_isDirectionFollowSnap360;
    h.dynamicMode = effect->m_isDynamicMode;
    h.dynamicBlock = effect->m_isDynamicBlock;
    h.useMoveTarget = effect->m_useMoveTarget;
    h.touchTriggered = effect->m_isTouchTriggered;
    h.spawnTriggered = effect->m_isSpawnTriggered;
    h.targetPlayer1 = effect->m_targetPlayer1;
    h.targetPlayer2 = effect->m_targetPlayer2;
    h.targetGroupId = effect->m_targetGroupID;
    h.centerGroupId = effect->m_centerGroupID;
    h.moveOffsetX = effect->m_moveOffset.x;
    h.moveOffsetY = effect->m_moveOffset.y;
    h.rotationDegrees = effect->m_rotationDegrees;
    h.duration = effect->m_duration;
    return h;
}

TriggerKind EbTriggerClassify::classify(GameObject* object) {
    if (!(!object)) {
        return EbTriggerClassify::classifyHints(EbTriggerClassify::hintsFromObject(object));
    }
    return TriggerKind::Ignore;
}

bool EbTriggerClassify::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    expect(EbTriggerClassify::classify(nullptr) == TriggerKind::Ignore, "null");

    MotionHints move{};
    move.isEffect = true;
    move.objectId = 901;
    move.moveOffsetX = 100.f;
    move.spawnTriggered = true;
    expect(EbTriggerClassify::classifyHints(move) == TriggerKind::StaticMove, "static-move-901");

    // Group 6 follows group 5 (Advanced Follow) — bakeable, not player.
    MotionHints groupFollow{};
    groupFollow.isEffect = true;
    groupFollow.objectId = 1347;
    groupFollow.targetGroupId = 6;
    groupFollow.centerGroupId = 5;
    groupFollow.useMoveTarget = true;
    expect(EbTriggerClassify::classifyHints(groupFollow) == TriggerKind::StaticMove,
           "group-follow-static");

    // Same follow ID but targets player → dynamic.
    MotionHints playerFollow = groupFollow;
    playerFollow.targetPlayer1 = true;
    expect(EbTriggerClassify::classifyHints(playerFollow) == TriggerKind::DynamicFollow,
           "player-target-dynamic");

    MotionHints followPlayerY{};
    followPlayerY.isEffect = true;
    followPlayerY.objectId = 1585;
    expect(EbTriggerClassify::classifyHints(followPlayerY) == TriggerKind::DynamicFollow,
           "follow-player-y");

    MotionHints lockPlayer{};
    lockPlayer.isEffect = true;
    lockPlayer.objectId = 901;
    lockPlayer.moveOffsetX = 50.f;
    lockPlayer.lockToPlayerX = true;
    expect(EbTriggerClassify::classifyHints(lockPlayer) == TriggerKind::DynamicFollow,
           "lock-player-x");

    MotionHints touchMove{};
    touchMove.isEffect = true;
    touchMove.objectId = 901;
    touchMove.moveOffsetX = 100.f;
    touchMove.touchTriggered = true;
    expect(EbTriggerClassify::classifyHints(touchMove) == TriggerKind::DynamicOther,
           "touch-move-dynamic");

    MotionHints cam{};
    cam.isEffect = true;
    cam.objectId = 1913;
    cam.lockToPlayerX = true;
    expect(EbTriggerClassify::classifyHints(cam) == TriggerKind::Ignore, "camera-ignore");

    MotionHints rotation{};
    rotation.isEffect = true;
    rotation.objectId = 1346;
    rotation.rotationDegrees = 90.f;
    rotation.spawnTriggered = true;
    expect(EbTriggerClassify::classifyHints(rotation) == TriggerKind::StaticRotate, "static-rotate");

    MotionHints dynModeGroup{};
    dynModeGroup.isEffect = true;
    dynModeGroup.objectId = 901;
    dynModeGroup.moveOffsetX = 10.f;
    dynModeGroup.dynamicMode = true;
    dynModeGroup.targetGroupId = 5;
    expect(EbTriggerClassify::classifyHints(dynModeGroup) == TriggerKind::StaticMove,
           "dynamic-mode-group-static");

    MotionHints pulse{};
    pulse.isEffect = true;
    pulse.objectId = 1006;
    expect(EbTriggerClassify::classifyHints(pulse) == TriggerKind::Ignore, "pulse-ignore");

    // Toggle enables/disables groups — must not be Ignore.
    MotionHints toggle{};
    toggle.isEffect = true;
    toggle.objectId = 1049;
    toggle.spawnTriggered = true;
    expect(EbTriggerClassify::classifyHints(toggle) == TriggerKind::StaticToggle, "static-toggle");

    MotionHints touchToggle{};
    touchToggle.isEffect = true;
    touchToggle.objectId = 1049;
    touchToggle.touchTriggered = true;
    expect(EbTriggerClassify::classifyHints(touchToggle) == TriggerKind::DynamicOther,
           "touch-toggle-dynamic");

    MotionHints toggleOrb{};
    toggleOrb.isEffect = true;
    toggleOrb.objectId = 1594;
    expect(EbTriggerClassify::classifyHints(toggleOrb) == TriggerKind::DynamicOther,
           "toggle-orb-dynamic");

    if (fails == 0) {
        report = "TriggerClassify selfCheck OK";
        return true;
    }
    report = fmt::format("TriggerClassify selfCheck {} fails: {}", fails, report);
    return false;
}
