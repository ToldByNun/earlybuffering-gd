#ifndef EARLYBUFFER_CORE_SIMCOLLISIONPOLICY_HPP
#define EARLYBUFFER_CORE_SIMCOLLISIONPOLICY_HPP

#include <string>

class GameObject;
class PlayerObject;

enum class SimObjectAction {
    Include = 0,
    UnknownOnOverlap,
    Skip
};

// Soft = Commit-5 Include (vehicle/mirror/teleport/modifier…). Hard = must
// truncate / Pass (toggle pre-model, player-follow, dual). No Soft-Continue.
enum class SimUnknownClass {
    Soft = 0,
    Hard
};

class SimCollisionPolicy {
public:
    static bool isSpeedPortalId(int objectId);
    static SimObjectAction classify(GameObject* obj);

    // Pure type table (PlanGold / fixtures) — no live PlayLayer.
    // objectType = (int)GameObjectType; objectId = m_objectID (speed portals).
    static SimObjectAction classifyObjectType(int objectType, int objectId = 0);
    static SimUnknownClass unknownClassForType(int objectType);

    // Soft vehicle/mirror/teleport/enter (Commit 5) — Include only for bakeOffline.
    // Live assist sims truncate: stacked portal activatedByPlayer mutates shared
    // layer/sections and was an AV source (read @ 0xFFF…).
    static bool softPortalLiveTruncate(int objectType);

    // Only meaningful when classify would be UnknownOnOverlap (Hard leftovers).
    static SimUnknownClass unknownClass(GameObject* obj);
    static char const* unknownClassId(SimUnknownClass c);
    static bool noteLimitedSimOnce();
    static bool selfCheck(std::string& report);

private:
    SimCollisionPolicy() = delete;
};

#endif
