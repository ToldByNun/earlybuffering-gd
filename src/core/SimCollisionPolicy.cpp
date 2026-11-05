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

SimUnknownClass SimCollisionPolicy::unknownClassForType(int objectType) {
    auto const t = static_cast<GameObjectType>(objectType);
    switch (t) {
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
            return SimUnknownClass::Hard;
        default:
            return SimUnknownClass::Soft;
    }
}

bool SimCollisionPolicy::softPortalLiveTruncate(int objectType) {
    auto const t = static_cast<GameObjectType>(objectType);
    switch (t) {
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
        case GameObjectType::TeleportPortal:
        case GameObjectType::TeleportOrb:
        case GameObjectType::Modifier:
        case GameObjectType::Special:
        case GameObjectType::EnterEffectObject:
            return true;
        default:
            return false;
    }
}

SimObjectAction SimCollisionPolicy::classifyObjectType(int objectType, int objectId) {
    if (isSpeedPortalId(objectId)) return SimObjectAction::Include;

    auto const t = static_cast<GameObjectType>(objectType);
    switch (t) {
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

        // Soft Unknown → Include (histogram Soft class; vanilla handles).
        // Evidence: Soft bins = vehicle/mirror/teleport/modifier/collision/enter.
        // Hard Dual/Solo stay UnknownOnOverlap (no Soft-Continue).
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
        case GameObjectType::TeleportPortal:
        case GameObjectType::TeleportOrb:
        case GameObjectType::Modifier:
        case GameObjectType::Special:
        case GameObjectType::EnterEffectObject:
            return SimObjectAction::Include;

        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
            return SimObjectAction::UnknownOnOverlap;

        default:
            return SimObjectAction::Skip;
    }
}

SimObjectAction SimCollisionPolicy::classify(GameObject* obj) {
    if (!obj) return SimObjectAction::Skip;

    auto& sim = ShadowSimulator::get();
    if (sim.isSimulating()) {
        // Live (not bakeOffline): never Include Soft vehicle/mirror/teleport —
        // stacked portal activatedByPlayer on shared objects poisons m_sections.
        if (!sim.bakeOffline()
            && softPortalLiveTruncate(static_cast<int>(obj->m_objectType))) {
            return SimObjectAction::UnknownOnOverlap;
        }

        // Toggle orb / touch-toggle: modeled flip → Include/Skip; else Hard Unknown
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
            // skip-dynamic (default) / dual / probe miss → Hard Unknown.
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

    return SimCollisionPolicy::classifyObjectType(
        static_cast<int>(obj->m_objectType),
        obj->m_objectID
);
}

SimUnknownClass SimCollisionPolicy::unknownClass(GameObject* obj) {
    if (!obj) return SimUnknownClass::Soft;
    auto& sim = ShadowSimulator::get();
    // Hard: honesty truncate / Pass — never soft-continue these.
    if (sim.isToggleTriggerTarget(obj->m_uniqueID)) return SimUnknownClass::Hard;
    if (sim.isDynamicTriggerTarget(obj->m_uniqueID)) return SimUnknownClass::Hard;
    return SimCollisionPolicy::unknownClassForType(
        static_cast<int>(obj->m_objectType)
);
}

char const* SimCollisionPolicy::unknownClassId(SimUnknownClass c) {
    return c == SimUnknownClass::Hard ? "hard" : "soft";
}

bool SimCollisionPolicy::noteLimitedSimOnce() {
    static bool warned = false;
    if (warned) return false;
    warned = true;
    AssistLog::info(
        "sim: limited (Hard Unknown: toggle pre-model / follow-dynamic / dual; "
        "Soft vehicle·mirror·teleport → Include bakeOffline only, live truncate; "
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

    // Fixtures: Soft histogram types → Include; Hard Dual/Solo → Unknown.
    auto const wave = static_cast<int>(GameObjectType::WavePortal);
    auto const ship = static_cast<int>(GameObjectType::ShipPortal);
    auto const cube = static_cast<int>(GameObjectType::CubePortal);
    auto const tele = static_cast<int>(GameObjectType::TeleportPortal);
    auto const teleOrb = static_cast<int>(GameObjectType::TeleportOrb);
    auto const mirror = static_cast<int>(GameObjectType::NormalMirrorPortal);
    auto const mod = static_cast<int>(GameObjectType::Modifier);
    auto const col = static_cast<int>(GameObjectType::CollisionObject);
    auto const enter = static_cast<int>(GameObjectType::EnterEffectObject);
    auto const dual = static_cast<int>(GameObjectType::DualPortal);
    auto const solo = static_cast<int>(GameObjectType::SoloPortal);
    auto const solid = static_cast<int>(GameObjectType::Solid);
    auto const grav = static_cast<int>(GameObjectType::NormalGravityPortal);

    expect(classifyObjectType(wave) == SimObjectAction::Include, "soft-wave-include");
    expect(classifyObjectType(ship) == SimObjectAction::Include, "soft-ship-include");
    expect(classifyObjectType(cube) == SimObjectAction::Include, "soft-cube-include");
    expect(classifyObjectType(tele) == SimObjectAction::Include, "soft-tele-include");
    expect(classifyObjectType(teleOrb) == SimObjectAction::Include, "soft-teleorb-include");
    expect(classifyObjectType(mirror) == SimObjectAction::Include, "soft-mirror-include");
    expect(classifyObjectType(mod) == SimObjectAction::Include, "soft-modifier-include");
    expect(classifyObjectType(col) == SimObjectAction::Include, "soft-collision-include");
    expect(classifyObjectType(enter) == SimObjectAction::Include, "soft-enter-include");
    expect(classifyObjectType(dual) == SimObjectAction::UnknownOnOverlap, "hard-dual-unk");
    expect(classifyObjectType(solo) == SimObjectAction::UnknownOnOverlap, "hard-solo-unk");
    expect(classifyObjectType(solid) == SimObjectAction::Include, "solid-include");
    expect(classifyObjectType(grav) == SimObjectAction::Include, "grav-include");
    expect(classifyObjectType(0, 200) == SimObjectAction::Include, "speed-id-include");

    expect(unknownClassForType(wave) == SimUnknownClass::Soft, "wave-soft-class");
    expect(unknownClassForType(dual) == SimUnknownClass::Hard, "dual-hard-class");
    expect(unknownClassForType(solo) == SimUnknownClass::Hard, "solo-hard-class");

    // No Soft-Continue: Soft is Include, not UnknownOnOverlap.
    expect(
        classifyObjectType(wave) != SimObjectAction::UnknownOnOverlap,
        "no-soft-continue-wave"
);

    expect(softPortalLiveTruncate(wave), "live-trunc-wave");
    expect(softPortalLiveTruncate(tele), "live-trunc-tele");
    expect(softPortalLiveTruncate(dual) == false, "live-trunc-not-dual");
    expect(softPortalLiveTruncate(grav) == false, "live-trunc-not-grav");
    expect(softPortalLiveTruncate(solid) == false, "live-trunc-not-solid");

    if (fails == 0) {
        report = "SimCollisionPolicy selfCheck OK";
        return true;
    }
    report = fmt::format("SimCollisionPolicy selfCheck {} fails: {}", fails, report);
    return false;
}
