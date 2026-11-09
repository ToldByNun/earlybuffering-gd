#ifndef SIM_COLLISION_POLICY_HPP
#define SIM_COLLISION_POLICY_HPP

#include <string>

class GameObject;
class PlayerObject;

enum class SimObjectAction { Include = 0, UnknownOnOverlap, Skip };

// Soft = Commit-5 Include (vehicle/mirror/teleport/modifier…). Hard = must
// truncate / Pass (toggle pre-model, player-follow, dual). No Soft-Continue.
enum class SimUnknownClass { Soft = 0, Hard };

class EbSimCollisionPolicy {
public:
    static bool isSpeedPortalId(int objectId);
    static SimObjectAction classify(GameObject* object);

    // Pure type table (PlanGold / fixtures) — no live PlayLayer.
    // objectType = (int)GameObjectType; objectId = m_objectID (speed portals).
    static SimObjectAction classifyObjectType(int objectType, int objectId = 0);
    static SimUnknownClass unknownClassForType(int objectType);

    // Only meaningful when classify would be UnknownOnOverlap (Hard leftovers).
    static SimUnknownClass unknownClass(GameObject* object);
    static char const* unknownClassId(SimUnknownClass candidate);
    static bool noteLimitedSimOnce();
    static bool selfCheck(std::string& report);

private:
    EbSimCollisionPolicy() = delete;
};

#endif
