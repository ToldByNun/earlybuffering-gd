#include "cpu_physics_world.hpp"
#include "../../level/section_object_walk.hpp"
#include "../../learning/neural_hitbox.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../foresight/travel_foresight.hpp"
#include "../sim_collision_policy.hpp"
#include "cpu_physics_seed.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

static void fillTwSamples(EbCpuPhysicsBody& body, int uniqueId, int baseAbsTick, int horizon,
                          float simDt, float baseX, float baseY, float baseW, float baseH) {
    body.twSamples = 0;
    if (uniqueId == 0 || horizon <= 0)
        return;
    auto& store = EbTriggerWorldStore::get();
    if (!store.loaded() || !store.findTrack(uniqueId))
        return;

    int const steps = std::clamp(horizon, 1, EbCpuPhysicsBody::kMaxTwSamples);
    int filled = 0;
    for (int index = 0; index < steps; ++index) {
        int const tickOff =
            steps == 1 ? 0 : (index * std::max(1, horizon - 1)) / std::max(1, steps - 1);
        EbTriggerPose pose;
        bool const ok = store.samplePose(uniqueId, baseAbsTick + tickOff, simDt, pose);
        body.twTick[filled] = static_cast<std::int16_t>(tickOff);
        if (ok && pose.active) {
            float const sx = std::max(0.05f, std::fabs(pose.scaleX));
            float const sy = std::max(0.05f, std::fabs(pose.scaleY));
            float const w = baseW * sx;
            float const h = baseH * sy;
            body.twX[filled] = pose.x - w * 0.5f;
            body.twY[filled] = pose.y - h * 0.5f;
            body.twW[filled] = w;
            body.twH[filled] = h;
        } else if (filled == 0) {
            body.twX[filled] = baseX;
            body.twY[filled] = baseY;
            body.twW[filled] = baseW;
            body.twH[filled] = baseH;
        } else {
            body.twX[filled] = body.twX[filled - 1];
            body.twY[filled] = body.twY[filled - 1];
            body.twW[filled] = body.twW[filled - 1];
            body.twH[filled] = body.twH[filled - 1];
        }
        ++filled;
    }
    body.twSamples = filled;
}

EbCpuPhysicsWorld EbCpuPhysicsWorldCapture::capture(PlayLayer* layer, PlayerObject* real,
                                                    Options const& options) {
    EbCpuPhysicsWorld world;
    if (!layer || !real)
        return world;

    world.seed = EbCpuPhysicsSeedIo::capture(real);
    if (!world.seed.valid)
        return world;

    world.horizonTicks = std::max(1, options.horizonTicks);
    world.simDt = options.simDt > 0.f ? options.simDt : (1.f / 240.f);
    world.baseAbsTick = std::max(0, options.baseAbsTick);

    float const px = real->getPositionX();
    float const py = real->getPositionY();
    float const x0 = px - std::max(0.f, options.behindX);
    float const x1 = px + std::max(40.f, options.aheadX);
    float const y0 = py - std::max(40.f, options.yReach);
    float const y1 = py + std::max(40.f, options.yReach);

    world.bodies.reserve(256);
    world.floorY = py - 240.f;
    world.ceilY = py + 240.f;

    EbSectionObjectWalk::forEachOwnedObject(layer, [&](GameObject* object) {
        if (!object)
            return;
        auto const type = object->m_objectType;
        auto const action =
            EbSimCollisionPolicy::classifyObjectType(static_cast<int>(type), object->m_objectID);
        if (action == SimObjectAction::Skip)
            return;
        // Hard unknowns (dual/solo) — skip so we don't invent outcomes; Adaptive
        // still Shadow-verifies survivors.
        if (action == SimObjectAction::UnknownOnOverlap)
            return;
        switch (type) {
        case GameObjectType::TeleportPortal:
        case GameObjectType::TeleportOrb:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
            return;
        default:
            break;
        }

        auto const box = EbNeuralHitbox::read(object);
        if (!box.valid || box.aabbW <= 0.f || box.aabbH <= 0.f)
            return;
        float const cx = box.aabbX + box.aabbW * 0.5f;
        float const cy = box.aabbY + box.aabbH * 0.5f;
        if (cx < x0 || cx > x1 || cy < y0 || cy > y1)
            return;

        EbCpuPhysicsBody body;
        body.object = object;
        body.x = box.aabbX;
        body.y = box.aabbY;
        body.w = box.aabbW;
        body.h = box.aabbH;
        body.uniqueId = object->m_uniqueID;
        body.objectType = static_cast<int>(type);
        body.isHazard = type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard;
        body.isRing = EbTravelForesight::isOrbLike(type);
        body.isPad = EbTravelForesight::isPadLike(type);
        body.savedIsActivated = object->m_isActivated;
        if (auto* enhanced = geode::prelude::typeinfo_cast<EnhancedGameObject*>(object)) {
            body.savedActivated = enhanced->m_activated;
            body.savedPoweredOn = enhanced->m_poweredOn;
            body.savedP1 = enhanced->m_activatedByPlayer1;
            body.savedP2 = enhanced->m_activatedByPlayer2;
        }
        if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object))
            body.savedClaimTouch = ring->m_claimTouch;

        fillTwSamples(body, body.uniqueId, world.baseAbsTick, world.horizonTicks, world.simDt,
                      body.x, body.y, body.w, body.h);
        world.bodies.push_back(body);

        if (!body.isHazard && !body.isRing && !body.isPad) {
            world.floorY = std::min(world.floorY, body.y - 4.f);
            world.ceilY = std::max(world.ceilY, body.y + body.h + 4.f);
        }
    });

    world.valid = true;
    return world;
}

bool EbCpuPhysicsWorldCapture::selfCheck(std::string& report) {
    report.clear();
    EbCpuPhysicsWorld empty;
    if (empty.valid) {
        report = "CpuPhysicsWorld selfCheck fails: empty-valid";
        return false;
    }
    std::string seedReport;
    if (!EbCpuPhysicsSeedIo::selfCheck(seedReport)) {
        report = seedReport;
        return false;
    }
    report = "CpuPhysicsWorld selfCheck OK";
    return true;
}
