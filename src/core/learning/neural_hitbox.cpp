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
    // Always refresh for objects we already culled to the local window.
    // Dirty-only skips left pooled/moved objects with stale OBB corners — that
    // drew hitboxes floating off their sprites. updateOrientedBox on ~100 nearby
    // objects is cheap; force-dirtying the whole level is what tanked FPS before.
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

bool EbNeuralHitbox::obbMatchesAabb(EbNeuralHitbox const& box) {
    float minX = box.cornerX[0];
    float maxX = box.cornerX[0];
    float minY = box.cornerY[0];
    float maxY = box.cornerY[0];
    for (int corner = 1; corner < 4; ++corner) {
        minX = std::min(minX, box.cornerX[corner]);
        maxX = std::max(maxX, box.cornerX[corner]);
        minY = std::min(minY, box.cornerY[corner]);
        maxY = std::max(maxY, box.cornerY[corner]);
    }
    float const aabbCx = box.aabbX + box.aabbW * 0.5f;
    float const aabbCy = box.aabbY + box.aabbH * 0.5f;
    float const obbCx = (minX + maxX) * 0.5f;
    float const obbCy = (minY + maxY) * 0.5f;
    // Pooled/stale OBB often sits far from the live AABB — reject it.
    if (std::fabs(obbCx - aabbCx) > 24.0f || std::fabs(obbCy - aabbCy) > 24.0f)
        return false;
    if ((maxX - minX) < box.aabbW * 0.25f || (maxY - minY) < box.aabbH * 0.25f)
        return false;
    return true;
}

bool EbNeuralHitbox::copyOrientedCorners(GameObject* object, EbNeuralHitbox& box) {
    if (!object)
        return false;
    // Unrotated objects: RobTop/MH draw the AABB. OBB equals AABB when fresh,
    // but a stale OBB is what made solids float off their tiles.
    float const rotation = object->getObjectRotation();
    if (!std::isfinite(rotation) || std::fabs(rotation) < 0.5f)
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
    if (!obbMatchesAabb(box))
        return false;
    box.oriented = true;
    return true;
}

EbNeuralHitbox EbNeuralHitbox::read(GameObject* object) {
    EbNeuralHitbox box;
    if (!object)
        return box;
    ensureFresh(object);
    // Copy by value — getObjectRect returns a ref to a reused member.
    cocos2d::CCRect const rect = object->getObjectRect();
    if (!finiteRect(rect))
        return box;

    box.valid = true;
    box.aabbX = rect.origin.x;
    box.aabbY = rect.origin.y;
    box.aabbW = rect.size.width;
    box.aabbH = rect.size.height;
    fillAabbCorners(box);

    // OBB only when the object is actually rotated; otherwise keep AABB corners
    // (same path OpenHack/RobTop debug draw uses for axis-aligned solids).
    (void)copyOrientedCorners(object, box);
    return box;
}

EbNeuralHitbox EbNeuralHitbox::readPlayer(PlayerObject* player) {
    EbNeuralHitbox box;
    if (!player)
        return box;
    ensureFresh(player);
    cocos2d::CCRect const rect = player->getObjectRect();
    if (!finiteRect(rect))
        return box;
    box.valid = true;
    box.aabbX = rect.origin.x;
    box.aabbY = rect.origin.y;
    box.aabbW = rect.size.width;
    box.aabbH = rect.size.height;
    fillAabbCorners(box);
    // Player collision is the AABB OpenHack draws; skip OBB so the viz matches MH.
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
    expect(obbMatchesAabb(sample), "obb-match-self");
    if (fails == 0) {
        report = "NeuralHitbox OK";
        return true;
    }
    report = std::string("NeuralHitbox fails: ") + report;
    return false;
}
