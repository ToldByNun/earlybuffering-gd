#ifndef EARLYBUFFER_CORE_TRIGGERCLASSIFY_HPP
#define EARLYBUFFER_CORE_TRIGGERCLASSIFY_HPP

#include <string>

class GameObject;

// How a GameObject / EffectGameObject relates to Trigger World Bake.
// Only collision-relevant motion (move/rotate/scale/spawn of solids/hazards)
// is ever Static* or Dynamic*. Camera, pulse, song, shake, etc. → Ignore
// whether "dynamic" or not — they cannot kill by themselves.
enum class TriggerKind {
    Ignore = 0,
    StaticMove,
    StaticRotate,
    StaticScale,
    StaticSpawn,
    DynamicFollow,
    DynamicOther
};

// Heuristic classifier for trigger bake (Commit 2).
// Prefers EffectGameObject motion flags from Geode bindings; falls back to
// well-known object IDs. Non-lethal effects are always Ignore.
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

    // Known GD trigger object IDs (2.1 / 2.2 community set).
    static bool isMoveTriggerId(int objectId);
    static bool isRotateTriggerId(int objectId);
    static bool isScaleTriggerId(int objectId);
    static bool isSpawnTriggerId(int objectId);
    static bool isFollowTriggerId(int objectId);
    // Camera / cosmetic / audio — never bake (cannot kill via object pose).
    static bool isNonLethalTriggerId(int objectId);
    // Move/rotate/scale/spawn/follow — only these can enter Static* / Dynamic*.
    static bool isLethalMotionCandidateId(int objectId);

    static bool selfCheck(std::string& report);

private:
    TriggerClassify() = default;

    static bool hasDynamicFollowHints(MotionHints const& hints);
    static bool hasDynamicOtherHints(MotionHints const& hints);
    static MotionHints hintsFromObject(GameObject* obj);
};

#endif // EARLYBUFFER_CORE_TRIGGERCLASSIFY_HPP
