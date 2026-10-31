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

// Soft = Include-candidate later (portals/modifiers). Hard = must Pass/truncate
// (toggle pre-model, player-follow, dual). Commit 3 histogram; Commit 5+ policy.
enum class SimUnknownClass {
    Soft = 0,
    Hard
};

class SimCollisionPolicy {
public:
    static bool isSpeedPortalId(int objectId);
    static SimObjectAction classify(GameObject* obj);
    // Only meaningful when classify would be UnknownOnOverlap.
    static SimUnknownClass unknownClass(GameObject* obj);
    static char const* unknownClassId(SimUnknownClass c);
    static bool noteLimitedSimOnce();
    static bool selfCheck(std::string& report);
};

#endif