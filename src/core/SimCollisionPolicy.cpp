#include "SimCollisionPolicy.hpp"
#include "ShadowSimulator.hpp"
#include "ToggleOrbSim.hpp"
#include "TriggerClassify.hpp"
#include "TriggerWorldStore.hpp"
#include "../config/StageConfig.hpp"
#include "../debug/AssistLog.hpp"

#include <Geode/Geode.hpp>
#include <cstring>
#include <fmt/format.h>

using namespace geode::prelude;

bool SimCollisionPolicy::isSpeedPortalId(int objectId) {
    return objectId == 200 || objectId == 201 || objectId == 202 || objectId == 203 || objectId == 1334;
}

SimObjectAction SimCollisionPolicy::classify(GameObject* obj) {
    if (!obj) return SimObjectAction::Skip;

    auto& sim = ShadowSimulator::get();
    if (sim.isSimulating()) {
        // Toggle orb / touch-toggle: modeled flip → Include/Skip; else Unknown
        // for live plan honesty. Offline bake trusts the current powered/visible
        // pose so a working macro is not aborted on every toggle-group solid.
        if (sim.isToggleTriggerTarget(obj->m_uniqueID)) {
            auto& toggle = ToggleOrbSim::get();
            if (toggle.isModeled(obj->m_uniqueID)) {
                return toggle.isModeledActive(obj->m_uniqueID)
                    ? SimObjectAction::Include
                    : SimObjectAction::Skip;
            }
            if (sim.bakeOffline()) {
                auto* ego = typeinfo_cast<EnhancedGameObject*>(obj);
                if (!obj->isVisible() || (ego && !ego->m_poweredOn)) {
                    return SimObjectAction::Skip;
                }
                return SimObjectAction::Include;
            }
            return SimObjectAction::UnknownOnOverlap;
        }

        if (StageConfig::triggerBake()) {
            // Ready static bake track + apply this sim tick → Include (active only).
            if (sim.triggerWorldAppliedThisTick()) {
                auto const* track = TriggerWorldStore::get().findTrack(obj->m_uniqueID);
                if (track
                    && track->ready
                    && TriggerClassify::isStatic(track->kind)) {
                    // writePose(inactive) clears visibility/poweredOn — do not Include.
                    auto* ego = typeinfo_cast<EnhancedGameObject*>(obj);
                    if (!obj->isVisible() || (ego && !ego->m_poweredOn)) {
                        return SimObjectAction::Skip;
                    }
                    return SimObjectAction::Include;
                }
            }

            // Player-coupled follow targets: never trust editor pose.
            // skip-dynamic (default) / dual / probe miss → Unknown.
            // skip-dynamic off + cheap live probe applied → Include.
            // Offline bake: Skip (don't abort the whole macro on follow geometry).
            if (sim.isDynamicTriggerTarget(obj->m_uniqueID)) {
                if (!StageConfig::triggerBakeSkipDynamic()
                    && sim.liveDynamicApplied(obj->m_uniqueID)) {
                    return SimObjectAction::Include;
                }
                if (sim.bakeOffline()) {
                    return SimObjectAction::Skip;
                }
                return SimObjectAction::UnknownOnOverlap;
            }
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
        case GameObjectType::AnimatedHazard:
            return SimObjectAction::Include;
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

SimUnknownClass SimCollisionPolicy::unknownClass(GameObject* obj) {
    if (!obj) return SimUnknownClass::Soft;
    auto& sim = ShadowSimulator::get();
    // Hard: honesty truncate / Pass — never soft-continue these.
    if (sim.isToggleTriggerTarget(obj->m_uniqueID)) return SimUnknownClass::Hard;
    if (sim.isDynamicTriggerTarget(obj->m_uniqueID)) return SimUnknownClass::Hard;
    switch (obj->m_objectType) {
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
            return SimUnknownClass::Hard;
        default:
            break;
    }
    return SimUnknownClass::Soft;
}

char const* SimCollisionPolicy::unknownClassId(SimUnknownClass c) {
    return c == SimUnknownClass::Hard ? "hard" : "soft";
}

bool SimCollisionPolicy::noteLimitedSimOnce() {
    static bool warned = false;
    if (warned) return false;
    warned = true;
    AssistLog::info(
        "sim: limited (toggle-orb/touch-toggle → unknown until ToggleOrbSim flip; "
        "follow-player/dynamic → unknown unless live probe; "
        "ready static bake → include when active)"
    );
    return true;
}

bool SimCollisionPolicy::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };
    expect(
        std::strcmp(unknownClassId(SimUnknownClass::Soft), "soft") == 0,
        "id-soft"
    );
    expect(
        std::strcmp(unknownClassId(SimUnknownClass::Hard), "hard") == 0,
        "id-hard"
    );
    expect(isSpeedPortalId(200), "speed-200");
    expect(!isSpeedPortalId(1), "not-speed-1");
    if (fails == 0) {
        report = "SimCollisionPolicy selfCheck OK";
        return true;
    }
    report = fmt::format("SimCollisionPolicy selfCheck {} fails: {}", fails, report);
    return false;
}
