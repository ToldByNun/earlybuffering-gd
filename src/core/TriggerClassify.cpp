#include "TriggerClassify.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

#include <cmath>

using namespace geode::prelude;

bool TriggerClassify::isMoveTriggerId(int objectId) {
    // 901 = Move; 1611 = Stop (motion family).
    return objectId == 901 || objectId == 1611;
}

bool TriggerClassify::isRotateTriggerId(int objectId) {
    return objectId == 1346;
}

bool TriggerClassify::isScaleTriggerId(int objectId) {
    return objectId == 2067;
}

bool TriggerClassify::isSpawnTriggerId(int objectId) {
    return objectId == 1268;
}

bool TriggerClassify::isFollowTriggerId(int objectId) {
    // Advanced Follow / Follow Player Y / Follow — move groups with a target.
    return objectId == 1347 || objectId == 1585 || objectId == 1817;
}

bool TriggerClassify::isNonLethalTriggerId(int objectId) {
    // Camera (zoom/move/offset/rotate/edge) — never affects solid poses by ID alone.
    if (objectId == 1913 || objectId == 1914 || objectId == 1915
        || objectId == 1916 || objectId == 1917 || objectId == 2015
        || objectId == 2016 || objectId == 2901) {
        return true;
    }
    // Cosmetic / audio / timing UI — do not bake.
    switch (objectId) {
        case 29:   // BG effect (legacy)
        case 1006: // Pulse
        case 1007: // Alpha
        case 1049: // Toggle (enable/disable — not a pose track)
        case 1520: // Shake
        case 1818: // Random
        case 3605: // Area fade-ish
        case 3606: // Song
        case 3607: // TimeWarp (dt, not object pose)
        case 3612: // SFX
        case 3613: // Edit Song
            return true;
        default:
            return false;
    }
}

bool TriggerClassify::isLethalMotionCandidateId(int objectId) {
    return TriggerClassify::isMoveTriggerId(objectId)
        || TriggerClassify::isRotateTriggerId(objectId)
        || TriggerClassify::isScaleTriggerId(objectId)
        || TriggerClassify::isSpawnTriggerId(objectId)
        || TriggerClassify::isFollowTriggerId(objectId);
}

bool TriggerClassify::isStatic(TriggerKind kind) {
    return kind == TriggerKind::StaticMove
        || kind == TriggerKind::StaticRotate
        || kind == TriggerKind::StaticScale
        || kind == TriggerKind::StaticSpawn;
}

bool TriggerClassify::isDynamic(TriggerKind kind) {
    return kind == TriggerKind::DynamicFollow
        || kind == TriggerKind::DynamicOther;
}

bool TriggerClassify::isMotion(TriggerKind kind) {
    return kind == TriggerKind::StaticMove
        || kind == TriggerKind::StaticRotate
        || kind == TriggerKind::StaticScale
        || kind == TriggerKind::DynamicFollow
        || kind == TriggerKind::DynamicOther;
}

char const* TriggerClassify::kindName(TriggerKind kind) {
    switch (kind) {
        case TriggerKind::Ignore: return "Ignore";
        case TriggerKind::StaticMove: return "StaticMove";
        case TriggerKind::StaticRotate: return "StaticRotate";
        case TriggerKind::StaticScale: return "StaticScale";
        case TriggerKind::StaticSpawn: return "StaticSpawn";
        case TriggerKind::DynamicFollow: return "DynamicFollow";
        case TriggerKind::DynamicOther: return "DynamicOther";
        default: return "?";
    }
}

bool TriggerClassify::hasDynamicFollowHints(MotionHints const& hints) {
    // Only meaningful on lethal motion candidates (move/follow of solids).
    if (hints.lockToPlayerX || hints.lockToPlayerY) return true;
    // Solids locked to camera still move in world and can kill.
    if (hints.lockToCameraX || hints.lockToCameraY) return true;
    if (hints.followCpp) return true;
    if (hints.rotateFollowP1 || hints.rotateFollowP2) return true;
    if (hints.directionFollowSnap360) return true;
    if (TriggerClassify::isFollowTriggerId(hints.objectId)) return true;
    return false;
}

bool TriggerClassify::hasDynamicOtherHints(MotionHints const& hints) {
    if (hints.dynamicMode || hints.dynamicBlock) return true;
    if (hints.useMoveTarget) return true;
    return false;
}

TriggerKind TriggerClassify::classifyHints(MotionHints const& hints) {
    if (!hints.isEffect && hints.objectId == 0) {
        return TriggerKind::Ignore;
    }

    int const id = hints.objectId;

    // Camera / pulse / song / shake / … — always out, even if "dynamic".
    if (TriggerClassify::isNonLethalTriggerId(id)) {
        return TriggerKind::Ignore;
    }

    // Anything that is not a known lethal-motion family → Ignore.
    // Do not mark unknown effects DynamicOther (would waste bake/live work).
    if (!TriggerClassify::isLethalMotionCandidateId(id)) {
        return TriggerKind::Ignore;
    }

    // Follow / lock-to-player on real movers — not static-cacheable.
    if (TriggerClassify::hasDynamicFollowHints(hints)) {
        return TriggerKind::DynamicFollow;
    }
    if (TriggerClassify::hasDynamicOtherHints(hints)) {
        return TriggerKind::DynamicOther;
    }

    bool const motionId = TriggerClassify::isMoveTriggerId(id)
        || TriggerClassify::isRotateTriggerId(id)
        || TriggerClassify::isScaleTriggerId(id);
    // Touch-activated motion: start time depends on player path.
    if (hints.touchTriggered && motionId) {
        return TriggerKind::DynamicOther;
    }

    if (TriggerClassify::isFollowTriggerId(id)) {
        return TriggerKind::DynamicFollow;
    }
    if (TriggerClassify::isMoveTriggerId(id)) {
        if (std::fabs(hints.moveOffsetX) < 1e-4f
            && std::fabs(hints.moveOffsetY) < 1e-4f
            && id == 1611) {
            return TriggerKind::Ignore;
        }
        return TriggerKind::StaticMove;
    }
    if (TriggerClassify::isRotateTriggerId(id)) {
        return TriggerKind::StaticRotate;
    }
    if (TriggerClassify::isScaleTriggerId(id)) {
        return TriggerKind::StaticScale;
    }
    if (TriggerClassify::isSpawnTriggerId(id)) {
        return TriggerKind::StaticSpawn;
    }

    return TriggerKind::Ignore;
}

TriggerClassify::MotionHints TriggerClassify::hintsFromObject(GameObject* obj) {
    MotionHints h;
    if (!obj) return h;

    h.objectId = obj->m_objectID;

    auto* effect = typeinfo_cast<EffectGameObject*>(obj);
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
    h.moveOffsetX = effect->m_moveOffset.x;
    h.moveOffsetY = effect->m_moveOffset.y;
    h.rotationDegrees = effect->m_rotationDegrees;
    h.duration = effect->m_duration;
    return h;
}

TriggerKind TriggerClassify::classify(GameObject* obj) {
    if (!obj) return TriggerKind::Ignore;
    return TriggerClassify::classifyHints(TriggerClassify::hintsFromObject(obj));
}

bool TriggerClassify::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += name;
    };

    expect(TriggerClassify::classify(nullptr) == TriggerKind::Ignore, "null");

    MotionHints empty{};
    expect(
        TriggerClassify::classifyHints(empty) == TriggerKind::Ignore,
        "empty-hints"
    );

    MotionHints move{};
    move.isEffect = true;
    move.objectId = 901;
    move.moveOffsetX = 100.f;
    move.spawnTriggered = true;
    expect(
        TriggerClassify::classifyHints(move) == TriggerKind::StaticMove,
        "static-move-901"
    );

    MotionHints touchMove = move;
    touchMove.touchTriggered = true;
    touchMove.spawnTriggered = false;
    expect(
        TriggerClassify::classifyHints(touchMove) == TriggerKind::DynamicOther,
        "touch-move-dynamic"
    );

    MotionHints follow{};
    follow.isEffect = true;
    follow.objectId = 1347;
    expect(
        TriggerClassify::classifyHints(follow) == TriggerKind::DynamicFollow,
        "follow-id"
    );

    MotionHints lockPlayer{};
    lockPlayer.isEffect = true;
    lockPlayer.objectId = 901;
    lockPlayer.moveOffsetX = 50.f;
    lockPlayer.lockToPlayerX = true;
    expect(
        TriggerClassify::classifyHints(lockPlayer) == TriggerKind::DynamicFollow,
        "lock-player-x"
    );

    // Camera zoom even with "dynamic" flags must be Ignore (cannot kill).
    MotionHints cam{};
    cam.isEffect = true;
    cam.objectId = 1913;
    cam.lockToPlayerX = true;
    cam.dynamicMode = true;
    expect(
        TriggerClassify::classifyHints(cam) == TriggerKind::Ignore,
        "camera-ignore"
    );
    expect(TriggerClassify::isNonLethalTriggerId(1914), "cam-move-id");
    expect(TriggerClassify::isNonLethalTriggerId(1917), "cam-rot-id");

    MotionHints rot{};
    rot.isEffect = true;
    rot.objectId = 1346;
    rot.rotationDegrees = 90.f;
    rot.spawnTriggered = true;
    expect(
        TriggerClassify::classifyHints(rot) == TriggerKind::StaticRotate,
        "static-rotate"
    );

    MotionHints scale{};
    scale.isEffect = true;
    scale.objectId = 2067;
    scale.spawnTriggered = true;
    expect(
        TriggerClassify::classifyHints(scale) == TriggerKind::StaticScale,
        "static-scale"
    );

    MotionHints spawn{};
    spawn.isEffect = true;
    spawn.objectId = 1268;
    expect(
        TriggerClassify::classifyHints(spawn) == TriggerKind::StaticSpawn,
        "static-spawn"
    );

    MotionHints dynMode{};
    dynMode.isEffect = true;
    dynMode.objectId = 901;
    dynMode.moveOffsetX = 10.f;
    dynMode.dynamicMode = true;
    expect(
        TriggerClassify::classifyHints(dynMode) == TriggerKind::DynamicOther,
        "dynamic-mode"
    );

    MotionHints pulse{};
    pulse.isEffect = true;
    pulse.objectId = 1006;
    pulse.moveOffsetX = 99.f;
    expect(
        TriggerClassify::classifyHints(pulse) == TriggerKind::Ignore,
        "pulse-ignore"
    );

    MotionHints unknownFx{};
    unknownFx.isEffect = true;
    unknownFx.objectId = 99999;
    unknownFx.moveOffsetX = 40.f;
    expect(
        TriggerClassify::classifyHints(unknownFx) == TriggerKind::Ignore,
        "unknown-fx-ignore"
    );

    expect(TriggerClassify::isDynamic(TriggerKind::DynamicFollow), "is-dynamic");
    expect(!TriggerClassify::isStatic(TriggerKind::DynamicFollow), "follow-not-static");
    expect(
        std::string(TriggerClassify::kindName(TriggerKind::StaticMove)) == "StaticMove",
        "kind-name"
    );

    if (fails == 0) {
        report = "TriggerClassify selfCheck OK";
        return true;
    }
    report = fmt::format("TriggerClassify selfCheck {} fails: {}", fails, report);
    return false;
}
