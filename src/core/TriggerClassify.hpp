#ifndef EARLYBUFFER_CORE_TRIGGERCLASSIFY_HPP
#define EARLYBUFFER_CORE_TRIGGERCLASSIFY_HPP

#include <string>

class GameObject;

// How a GameObject / EffectGameObject relates to Trigger World Bake.
// Collision-relevant: move/rotate/scale/spawn/toggle of solids/hazards.
// Camera, pulse, song, shake, etc. → Ignore.
//
// Important: "follow another group/object" is STATIC (deterministic chain).
// Only follow/lock that depends on the live PLAYER is Dynamic*.
// Toggle (1049) is NOT Ignore — it enables/disables collideables.
enum class TriggerKind {
    Ignore = 0,
    StaticMove,
    StaticRotate,
    StaticScale,
    StaticSpawn,
    StaticToggle,
    DynamicFollow,
    DynamicOther
};

// Heuristic classifier for trigger bake (Commit 2).
class TriggerClassify {
public:
    // Flat flag bundle so selfCheck / tests need no live PlayLayer objects.
    struct MotionHints {
        int objectId = 0;
        bool isEffect = false;
        bool lockToPlayerX = false;
        bool lockToPlayerY = false;
        bool lockToCameraX = false;
        bool lockToCameraY = false;
        bool followCpp = false;
        bool rotateFollowP1 = false;
        bool rotateFollowP2 = false;
        bool directionFollowSnap360 = false;
        bool dynamicMode = false;
        bool dynamicBlock = false;
        bool useMoveTarget = false;
        bool touchTriggered = false;
        bool spawnTriggered = false;
        bool targetPlayer1 = false;
        bool targetPlayer2 = false;
        int targetGroupId = 0;
        int centerGroupId = 0;
        float moveOffsetX = 0.f;
        float moveOffsetY = 0.f;
        float rotationDegrees = 0.f;
        float duration = 0.f;
    };

    static TriggerKind classify(GameObject* obj);
    static TriggerKind classifyHints(MotionHints const& hints);

    static bool isStatic(TriggerKind kind);
    static bool isDynamic(TriggerKind kind);
    static bool isMotion(TriggerKind kind);
    static char const* kindName(TriggerKind kind);

    static bool isMoveTriggerId(int objectId);
    static bool isRotateTriggerId(int objectId);
    static bool isScaleTriggerId(int objectId);
    static bool isSpawnTriggerId(int objectId);
    // Toggle group on/off (collision-relevant visibility/active).
    static bool isToggleTriggerId(int objectId);
    // Follow-family IDs (may be group-follow = static, or player-follow = dynamic).
    static bool isFollowTriggerId(int objectId);
    // Explicit Follow Player Y (always player-coupled).
    static bool isFollowPlayerTriggerId(int objectId);
    static bool isNonLethalTriggerId(int objectId);
    // Collision-relevant candidates (motion + toggle), not camera/cosmetic.
    static bool isLethalMotionCandidateId(int objectId);

    static bool selfCheck(std::string& report);

private:
    TriggerClassify() = default;

    // True only when motion depends on the live player (not another group).
    static bool dependsOnLivePlayer(MotionHints const& hints);
    // Player-independent extras that still break a fixed timeline (e.g. touch start).
    static bool hasNonPlayerDynamicHints(MotionHints const& hints);
    static MotionHints hintsFromObject(GameObject* obj);
};

#endif // EARLYBUFFER_CORE_TRIGGERCLASSIFY_HPP
