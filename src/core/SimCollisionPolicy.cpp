#include "SimCollisionPolicy.hpp"
#include "ShadowSimulator.hpp"
#include "TriggerClassify.hpp"
#include "TriggerWorldStore.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

bool SimCollisionPolicy::isSpeedPortalId(int objectId) {
    return objectId == 200 || objectId == 201 || objectId == 202 || objectId == 203 || objectId == 1334;
}

SimObjectAction SimCollisionPolicy::classify(GameObject* obj) {
    if (!obj) return SimObjectAction::Skip;

    // Ready static bake track + apply this sim tick → trust collision (Include).
    // Partial build (!ready) and dynamics stay on the base path / Unknown.
    if (StageConfig::triggerBake()
        && ShadowSimulator::get().isSimulating()
        && ShadowSimulator::get().triggerWorldAppliedThisTick()) {
        auto const* track = TriggerWorldStore::get().findTrack(obj->m_uniqueID);
        if (track
            && track->ready
            && TriggerClassify::isStatic(track->kind)) {
            return SimObjectAction::Include;
        }
    }

    if (isSpeedPortalId(obj->m_objectID)) return SimObjectAction::Include;
    switch (obj->m_objectType) {
        case GameObjectType::Solid:
        case GameObjectType::Hazard:
        case GameObjectType::Slope:
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::GravityTogglePortal:
        case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::SpiderPad:
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::CustomRing:
        case GameObjectType::Breakable:
            return SimObjectAction::Include;
        case GameObjectType::AnimatedHazard:
            // Moving/animated hazards: treat as unknown on overlap (zone may be wrong).
            return SimObjectAction::UnknownOnOverlap;
        case GameObjectType::CollisionObject:
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::InverseMirrorPortal:
        case GameObjectType::NormalMirrorPortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
        case GameObjectType::TeleportPortal:
        case GameObjectType::TeleportOrb:
        case GameObjectType::Modifier:
            return SimObjectAction::UnknownOnOverlap;
        default:
            // Triggers / effects / unknown types -> unknown if overlapping
            if (obj->m_objectType == GameObjectType::Special ||
                obj->m_objectType == GameObjectType::EnterEffectObject) {
                return SimObjectAction::UnknownOnOverlap;
            }
            return SimObjectAction::Skip;
    }
}

bool SimCollisionPolicy::noteLimitedSimOnce() {
    static bool warned = false;
    if (warned) return false;
    warned = true;
    AssistLog::info(
        "sim: limited (dynamic/unbaked movers → unknown; ready static bake → include)"
    );
    return true;
}
