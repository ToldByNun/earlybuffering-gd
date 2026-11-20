#include "neural_hitbox.hpp"
#include <algorithm>
#include <cmath>

bool EbNeuralHitbox::finitePoint(float x, float y) {
    return std::isfinite(x) && std::isfinite(y);
}

bool EbNeuralHitbox::finiteRect(cocos2d::CCRect const& rect) {
    return finitePoint(rect.origin.x, rect.origin.y) && std::isfinite(rect.size.width) &&
           std::isfinite(rect.size.height) && rect.size.width > 0.05f && rect.size.height > 0.05f;
}

void EbNeuralHitbox::ensureFresh(GameObject* object) {
    if (!object)
        return;
    // Only recompute when GD says the cached rect/OBB is stale. Forcing dirty
    // on every object every tick made Visualize AI drop to single-digit FPS.
    if (object->getObjectRectDirty() || object->getOrientedRectDirty())
        object->updateOrientedBox();
}

void EbNeuralHitbox::fillAabbCorners(EbNeuralHitbox& box) {
    box.cornerX[0] = box.aabbX;
    box.cornerY[0] = box.aabbY;
    box.cornerX[1] = box.aabbX + box.aabbW;
    box.cornerY[1] = box.aabbY;
    box.cornerX[2] = box.aabbX + box.aabbW;
    box.cornerY[2] = box.aabbY + box.aabbH;
    box.cornerX[3] = box.aabbX;
    box.cornerY[3] = box.aabbY + box.aabbH;
    box.oriented = false;
}

bool EbNeuralHitbox::copyOrientedCorners(GameObject* object, EbNeuralHitbox& box) {
    if (!object)
        return false;
    auto* obb = object->getOrientedBox();
    if (!obb)
        return false;
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    for (int corner = 0; corner < 4; ++corner) {
        float const ox = obb->m_corners[static_cast<size_t>(corner)].x;
        float const oy = obb->m_corners[static_cast<size_t>(corner)].y;
        if (!finitePoint(ox, oy))
            return false;
        box.cornerX[corner] = ox;
        box.cornerY[corner] = oy;
        if (corner == 0) {
            minX = maxX = ox;
            minY = maxY = oy;
        } else {
            minX = std::min(minX, ox);
            maxX = std::max(maxX, ox);
            minY = std::min(minY, oy);
            maxY = std::max(maxY, oy);
        }
    }
    if ((maxX - minX) <= 0.05f || (maxY - minY) <= 0.05f)
        return false;
    box.oriented = true;
    return true;
}

EbNeuralHitbox EbNeuralHitbox::read(GameObject* object) {
    EbNeuralHitbox box;
    if (!object)
        return box;
    ensureFresh(object);
    cocos2d::CCRect const& rect = object->getObjectRect();
    if (!finiteRect(rect))
        return box;

    box.valid = true;
    box.aabbX = rect.origin.x;
    box.aabbY = rect.origin.y;
    box.aabbW = rect.size.width;
    box.aabbH = rect.size.height;
    fillAabbCorners(box);

    // OBB corners carry rotation; AABB alone is axis-aligned and wrong for
    // rotated spikes/saws. Fall back to AABB corners when OBB is unavailable.
    (void)copyOrientedCorners(object, box);
    return box;
}

EbNeuralHitbox EbNeuralHitbox::readPlayer(PlayerObject* player) {
    EbNeuralHitbox box;
    if (!player)
        return box;
    ensureFresh(player);
    cocos2d::CCRect const& rect = player->getObjectRect();
    if (!finiteRect(rect))
        return box;
    box.valid = true;
    box.aabbX = rect.origin.x;
    box.aabbY = rect.origin.y;
    box.aabbW = rect.size.width;
    box.aabbH = rect.size.height;
    fillAabbCorners(box);
    return box;
}

bool EbNeuralHitbox::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    expect(read(nullptr).valid == false, "null-invalid");
    expect(readPlayer(nullptr).valid == false, "null-player-invalid");
    EbNeuralHitbox sample;
    sample.aabbX = 10.0f;
    sample.aabbY = 20.0f;
    sample.aabbW = 30.0f;
    sample.aabbH = 40.0f;
    fillAabbCorners(sample);
    expect(!sample.oriented, "aabb-not-oriented");
    expect(sample.cornerX[0] == 10.0f && sample.cornerY[0] == 20.0f, "aabb-c0");
    expect(sample.cornerX[2] == 40.0f && sample.cornerY[2] == 60.0f, "aabb-c2");
    if (fails == 0) {
        report = "NeuralHitbox OK";
        return true;
    }
    report = std::string("NeuralHitbox fails: ") + report;
    return false;
}
