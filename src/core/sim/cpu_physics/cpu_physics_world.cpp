#include "cpu_physics_world.hpp"
#include "../../../config/stage_config.hpp"
#include "../../level/section_object_walk.hpp"
#include "../../learning/neural_hitbox.hpp"
#include "../foresight/travel_foresight.hpp"
#include "../sim_collision_policy.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

static std::uint8_t boostClass(GameObjectType type) {
    switch (type) {
    case GameObjectType::YellowJumpPad:
    case GameObjectType::YellowJumpRing:
        return 0;
    case GameObjectType::PinkJumpPad:
    case GameObjectType::PinkJumpRing:
        return 1;
    case GameObjectType::RedJumpPad:
    case GameObjectType::RedJumpRing:
        return 2;
    case GameObjectType::GravityPad:
    case GameObjectType::GravityRing:
    case GameObjectType::GreenRing:
        return 3;
    default:
        return 4;
    }
}

static std::uint8_t bodyKind(GameObjectType type) {
    if (EbTravelForesight::isPadLike(type))
        return 2;
    if (EbTravelForesight::isOrbLike(type))
        return 3;
    switch (type) {
    case GameObjectType::Hazard:
    case GameObjectType::AnimatedHazard:
        return 1;
    default:
        return 0;
    }
}

EbCpuPhysicsWorld EbCpuPhysicsWorldCapture::capture(PlayLayer* layer, PlayerObject* real,
                                                    float aheadX, float yReach, float behindX) {
    EbCpuPhysicsWorld world;
    if (!layer || !real)
        return world;

    auto const playerBox = EbNeuralHitbox::readPlayer(real);
    if (!playerBox.valid)
        return world;

    world.player.x = playerBox.aabbX;
    world.player.y = playerBox.aabbY;
    world.player.w = playerBox.aabbW;
    world.player.h = playerBox.aabbH;
    world.player.yVel = static_cast<float>(real->m_yVelocity);
    world.player.gravity =
        std::isfinite(real->m_gravity) && real->m_gravity > 0.01f ? real->m_gravity : 0.958972f;
    world.player.gravityMod =
        std::isfinite(real->m_gravityMod) ? real->m_gravityMod : 1.f;
    world.player.playerSpeed =
        std::isfinite(real->m_playerSpeed) && real->m_playerSpeed > 0.05f ? real->m_playerSpeed
                                                                         : 1.f;
    world.player.vehicleSize =
        std::isfinite(real->m_vehicleSize) && real->m_vehicleSize > 0.05f ? real->m_vehicleSize
                                                                         : 1.f;
    world.player.upsideDown = real->m_isUpsideDown;
    world.player.grounded = real->m_isOnGround || real->m_isOnGround2;
    world.player.holdingJump =
        real->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
    world.player.mode = static_cast<int>(EbStageConfig::kindOf(real));
    world.player.continuous = real->m_isShip || real->m_isDart || real->m_isSwing || real->m_isBird;
    world.player.tapRelease = real->m_isRobot || (!world.player.continuous && !real->m_isBall &&
                                                  !real->m_isSpider);

    float const px = real->getPositionX();
    float const py = real->getPositionY();
    float const x0 = px - std::max(0.f, behindX);
    float const x1 = px + std::max(40.f, aheadX);
    float const y0 = py - std::max(40.f, yReach);
    float const y1 = py + std::max(40.f, yReach);

    world.bodies.reserve(256);
    world.floorY = py - 240.f;
    world.ceilY = py + 240.f;

    EbSectionObjectWalk::forEachOwnedObject(layer, [&](GameObject* object) {
        if (!object)
            return;
        auto const type = object->m_objectType;
        auto const action =
            EbSimCollisionPolicy::classifyObjectType(static_cast<int>(type), object->m_objectID);
        if (action == SimObjectAction::Skip || action == SimObjectAction::UnknownOnOverlap)
            return;
        // Mode portals mid-screen are not modeled — Adaptive falls back to Shadow.
        switch (type) {
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::TeleportPortal:
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
        body.x = box.aabbX;
        body.y = box.aabbY;
        body.w = box.aabbW;
        body.h = box.aabbH;
        body.kind = bodyKind(type);
        body.boost = boostClass(type);
        world.bodies.push_back(body);

        if (body.kind == 0) {
            world.floorY = std::min(world.floorY, body.y - 4.f);
            world.ceilY = std::max(world.ceilY, body.y + body.h + 4.f);
        }
    });

    world.valid = true;
    return world;
}

bool EbCpuPhysicsWorldCapture::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += label;
    };

    expect(bodyKind(GameObjectType::Hazard) == 1, "hazard-kind");
    expect(bodyKind(GameObjectType::Solid) == 0, "solid-kind");
    expect(bodyKind(GameObjectType::YellowJumpPad) == 2, "pad-kind");
    expect(bodyKind(GameObjectType::YellowJumpRing) == 3, "orb-kind");
    expect(boostClass(GameObjectType::RedJumpRing) == 2, "red-boost");

    EbCpuPhysicsWorld empty;
    expect(!empty.valid, "empty-invalid");

    if (fails == 0) {
        report = "CpuPhysicsWorld selfCheck OK";
        return true;
    }
    report = fmt::format("CpuPhysicsWorld selfCheck {} fails: {}", fails, report);
    return false;
}
