#include "dev_ghost_diagnostics.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../core/assist/assist_clock.hpp"
#include "../../../core/assist/assist_controller.hpp"
#include "../../../core/bake/bake_map.hpp"
#include "../../../core/level/section_object_walk.hpp"
#include "../../../core/sim/shadow_simulator.hpp"
#include "../dev_diag_dump.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

void DevGhostDiagnostics::dumpVehicleFlags(std::ostringstream& o, char const* prefix,
                                           PlayerObject* player) {
    if (!player) {
        o << prefix << "=<null>\n";
        return;
    }
    o << prefix << ".x=" << player->getPositionX() << "\n";
    o << prefix << ".y=" << player->getPositionY() << "\n";
    o << prefix << ".yVel=" << player->m_yVelocity << "\n";
    o << prefix << ".dead=" << DevDiagDumpImplementation::boolStr(player->m_isDead) << "\n";
    o << prefix << ".mode=" << static_cast<int>(StageConfig::kindOf(player)) << " ("
      << StageConfig::kindName(StageConfig::kindOf(player)) << ")\n";
    o << prefix << ".isDart=" << DevDiagDumpImplementation::boolStr(player->m_isDart) << "\n";
    o << prefix << ".isShip=" << DevDiagDumpImplementation::boolStr(player->m_isShip) << "\n";
    o << prefix << ".isBall=" << DevDiagDumpImplementation::boolStr(player->m_isBall) << "\n";
    o << prefix << ".isBird=" << DevDiagDumpImplementation::boolStr(player->m_isBird) << "\n";
    o << prefix << ".isRobot=" << DevDiagDumpImplementation::boolStr(player->m_isRobot) << "\n";
    o << prefix << ".isSpider=" << DevDiagDumpImplementation::boolStr(player->m_isSpider) << "\n";
    o << prefix << ".isSwing=" << DevDiagDumpImplementation::boolStr(player->m_isSwing) << "\n";
    o << prefix << ".onGround=" << DevDiagDumpImplementation::boolStr(player->m_isOnGround) << "\n";
    o << prefix << ".jumpHeld="
      << DevDiagDumpImplementation::boolStr(
             player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)])
      << "\n";
}

bool DevGhostDiagnostics::isHazardType(GameObject* object) {
    if (!object)
        return false;
    auto const tick = object->m_objectType;
    return tick == GameObjectType::Hazard || tick == GameObjectType::AnimatedHazard;
}

void DevGhostDiagnostics::dumpRectLine(std::ostringstream& o, char const* name,
                                       PlayerObject* player) {
    if (!player) {
        o << name << "=<null>\n";
        return;
    }
    auto const result = player->getObjectRect();
    o << fmt::format("{}: pos=({:.2f},{:.2f}) rot={:.2f} scale=({:.3f},{:.3f}) vehicleSize={:.3f} "
                     "content=({:.2f},{:.2f}) rect=({:.2f},{:.2f},{:.2f},{:.2f}) area={:.1f} "
                     "jumpHeld={} isDart={}\n",
                     name, player->getPositionX(), player->getPositionY(), player->getRotation(),
                     player->getScaleX(), player->getScaleY(), player->m_vehicleSize,
                     player->getContentSize().width, player->getContentSize().height,
                     result.origin.x, result.origin.y, result.size.width, result.size.height,
                     result.size.width * result.size.height,
                     DevDiagDumpImplementation::boolStr(
                         player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)]),
                     DevDiagDumpImplementation::boolStr(player->m_isDart));
}

int DevGhostDiagnostics::countHazardOverlaps(PlayLayer* layer, PlayerObject* player,
                                             std::ostringstream& detail, int maxDetail) {
    if (!layer || !player)
        return 0;
    auto const pref = player->getObjectRect();
    int countValue = 0;
    auto consider = [&](GameObject* object) {
        if (!DevDiagDumpImplementation::isHazardType(object))
            return;
        auto const hr = object->getObjectRect();
        if (!pref.intersectsRect(hr))
            return;
        ++countValue;
        if (countValue <= maxDetail) {
            detail << fmt::format(
                "  hazard id={} type={} @({:.1f},{:.1f}) rect=({:.1f},{:.1f},{:.1f},{:.1f})\n",
                object->m_uniqueID, static_cast<int>(object->m_objectType), object->getPositionX(),
                object->getPositionY(), hr.origin.x, hr.origin.y, hr.size.width, hr.size.height);
        }
    };
    if (auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr) {
        for (unsigned index = 0; index < children->count(); ++index) {
            // Same live-pointer filter as sections — children can be stale too.
            consider(SectionObjectWalk::asGameObject(children->objectAtIndex(index)));
        }
    }
    // Never raw typeinfo_cast on m_sections — poison slots AV (bakefail diag).
    SectionObjectWalk::forEachInSections(layer, consider);
    return countValue;
}

std::string DevGhostDiagnostics::buildGhostHitbox() {
    std::ostringstream o;
    o << "# Ghost vs live hitbox after copyPlayerState(startSnap) — BEFORE physics\n";
    o << "# If shadow rect >> P1 or exclusive hazard overlaps → false frame-0 deaths\n\n";

    auto* layer = PlayLayer::get();
    if (!layer || !layer->m_player1) {
        o << "ERROR: no PlayLayer / P1\n";
        return o.str();
    }
    auto& sim = ShadowSimulator::get();
    auto* snap = sim.snapshotSlot(ShadowSimulator::START_SNAPSHOT_SLOT);
    auto* shadow = sim.shadow();
    if (!snap || !shadow) {
        o << "ERROR: missing start snap or shadow — reset once in level\n";
        return o.str();
    }

    // Ensure start snap exists for this attempt.
    (void)AssistController::get().ensureStartSnapshot(layer);
    snap = sim.snapshotSlot(ShadowSimulator::START_SNAPSHOT_SLOT);

    DevDiagDumpImplementation::dumpRectLine(o, "liveP1(before)", layer->m_player1);
    DevDiagDumpImplementation::dumpRectLine(o, "startSnap", snap);
    DevDiagDumpImplementation::dumpRectLine(o, "shadow(beforeCopy)", shadow);

    ShadowSimulator::copyPlayerState(shadow, snap);
    DevDiagDumpImplementation::dumpRectLine(o, "shadow(afterCopy)", shadow);

    auto const p1r = layer->m_player1->getObjectRect();
    auto const shr = shadow->getObjectRect();
    o << fmt::format("rectDelta: dw={:.2f} dh={:.2f} dArea={:.1f}\n",
                     shr.size.width - p1r.size.width, shr.size.height - p1r.size.height,
                     (shr.size.width * shr.size.height) - (p1r.size.width * p1r.size.height));

    std::ostringstream p1Haz;
    std::ostringstream shHaz;
    int const p1Hits =
        DevDiagDumpImplementation::countHazardOverlaps(layer, layer->m_player1, p1Haz, 12);
    int const shHits = DevDiagDumpImplementation::countHazardOverlaps(layer, shadow, shHaz, 12);
    o << fmt::format("hazardOverlaps liveP1={} shadow={}\n", p1Hits, shHits);
    if (p1Hits > 0) {
        o << "liveP1 overlaps:\n" << p1Haz.str();
    }
    if (shHits > 0) {
        o << "shadow overlaps:\n" << shHaz.str();
    }
    o << fmt::format("exclusiveShadowHazards={}\n",
                     shHits > p1Hits ? "LIKELY (ghost hitbox/world mismatch)" : "none-or-equal");

    // TW capture size + bake gdStep for current pending replay dt.
    o << fmt::format("twCapturedTransforms={}\n", sim.diagCaptureTransformCount(layer));

    float deltaTime = AssistController::get().simDt();
    if (!(deltaTime > 0.f))
        deltaTime = AssistClock::kNominalSimDt;
    auto const* report = BakeMap::get().pendingReplay();
    if (report && report->tps > AssistClock::kPlanSnapTpsHi) {
        deltaTime = static_cast<float>(1.0 / std::min(report->tps, 4000.0));
    }
    o << fmt::format("probeDt={} gdStep={}\n", deltaTime, AssistClock::secondsToGdStep(deltaTime));
    return o.str();
}
