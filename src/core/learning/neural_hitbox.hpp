#ifndef NEURAL_HITBOX_HPP
#define NEURAL_HITBOX_HPP

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>

// Ground-truth Geometry Dash collision geometry for the neural scorer.
// Primary: getObjectRect AABB (same path RobTop/OpenHack debug draw).
// OBB corners only when |rotation| >= 0.5° and the OBB still sits on that AABB
// (rejects stale pooled OBBs). Always updateOrientedBox on the culled window —
// never invents sprite contentSize boxes.
class EbNeuralHitbox {
public:
    bool valid = false;
    bool oriented = false;
    float aabbX = 0.0f;
    float aabbY = 0.0f;
    float aabbW = 0.0f;
    float aabbH = 0.0f;
    float cornerX[4]{};
    float cornerY[4]{};

    static EbNeuralHitbox read(GameObject* object);
    static EbNeuralHitbox readPlayer(PlayerObject* player);

    static bool selfCheck(std::string& report);

private:
    static bool finiteRect(cocos2d::CCRect const& rect);
    static bool finitePoint(float x, float y);
    static void ensureFresh(GameObject* object);
    static void fillAabbCorners(EbNeuralHitbox& box);
    static bool copyOrientedCorners(GameObject* object, EbNeuralHitbox& box);
    static bool obbMatchesAabb(EbNeuralHitbox const& box);
};

#endif
