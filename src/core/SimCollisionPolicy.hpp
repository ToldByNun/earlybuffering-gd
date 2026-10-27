#ifndef FRIRECRACKER_CORE_SIMCOLLISIONPOLICY_HPP
#define FRIRECRACKER_CORE_SIMCOLLISIONPOLICY_HPP

class GameObject;
class PlayerObject;

enum class SimObjectAction {
    Include = 0,
    UnknownOnOverlap,
    Skip
};

class SimCollisionPolicy {
public:
    static bool isSpeedPortalId(int objectId);
    static SimObjectAction classify(GameObject* obj);
    static bool noteLimitedSimOnce();
};

#endif