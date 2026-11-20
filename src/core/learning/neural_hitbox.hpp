#ifndef NEURAL_HITBOX_HPP
#define NEURAL_HITBOX_HPP

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>

// Ground-truth Geometry Dash collision geometry for the neural scorer.
// Reads getObjectRect / getOrientedBox. Only calls updateOrientedBox when GD
// already marked the rect dirty — never force-dirties the whole level (that
// caused ~7 FPS with Visualize AI). Never invents sprite contentSize boxes.
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
};

#endif
