#include "../../../app/class_manager.hpp"
#include "orb_stack_resolver.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../trigger_world/trigger_classify.hpp"
#include "../foresight/travel_foresight.hpp"
#include "../shadow_simulator.hpp"
#include "toggle_orb_sim.hpp"
#include <cmath>
#include <fmt/format.h>

EbOrbStackResolver& EbOrbStackResolver::get() {
    return EbClassManager::get().service<EbOrbStackResolver>();
}

void EbOrbStackResolver::begin() {
    this->m_stats = {};
    this->m_log.clear();
}

bool EbOrbStackResolver::isJumpOrDashRingType(GameObjectType type) {
    return EbTravelForesight::isOrbLike(type);
}

bool EbOrbStackResolver::isJumpOrDashRing(GameObject* object) {
    if (!object)
        return false;
    if (!(EbTriggerClassify::isToggleOrbId(object->m_objectID))) {
        return EbOrbStackResolver::isJumpOrDashRingType(object->m_objectType);
    }
    return false;
}

bool EbOrbStackResolver::withinColocateEps(GameObject* firstValue, GameObject* secondValue) {
    if (!firstValue || !secondValue)
        return false;
    float const deltaX = firstValue->getPositionX() - secondValue->getPositionX();
    float const deltaY = firstValue->getPositionY() - secondValue->getPositionY();
    return (deltaX * deltaX + deltaY * deltaY) <=
           (EbOrbStackResolver::kColocateEps * EbOrbStackResolver::kColocateEps);
}

bool EbOrbStackResolver::shouldSkipToggleFlip(GameObject* member, GameObject* toggleHit) {
    if (!member)
        return false;
    if (EbOrbStackResolver::isJumpOrDashRing(member))
        return true;
    if (toggleHit && geode::prelude::typeinfo_cast<RingObject*>(member) &&
        !EbTriggerClassify::isToggleOrbId(member->m_objectID) &&
        EbOrbStackResolver::withinColocateEps(member, toggleHit)) {
        return true;
    }
    return false;
}

void EbOrbStackResolver::noteFlipSkipped() {
    ++this->m_stats.flipSkipped;
}

void EbOrbStackResolver::noteTouch(PlayerObject* shadow, RingObject* ring, bool appliedJump,
                                 int simTick) {
    if (!ring)
        return;

    bool const toggle = EbTriggerClassify::isToggleOrbId(ring->m_objectID);
    bool const jumpRing = EbOrbStackResolver::isJumpOrDashRing(ring);
    bool jumpHeld = false;
    if (shadow) {
        int const jumpIdx = static_cast<int>(PlayerButton::Jump);
        jumpHeld = shadow->m_holdingButtons[jumpIdx];
    }
    bool const claimed = ring->m_claimTouch || ring->m_activated;

    TouchEvent ev;
    ev.oid = ring->m_objectID;
    ev.uid = ring->m_uniqueID;
    ev.toggle = toggle;
    ev.jumpRing = jumpRing;
    ev.jumpHeld = jumpHeld;
    ev.claimed = claimed;
    ev.appliedJump = appliedJump;
    ev.x = ring->getPositionX();
    ev.y = ring->getPositionY();
    ev.tick = simTick;

    ++this->m_stats.touchCount;
    if (toggle)
        ++this->m_stats.toggleTouches;
    if (jumpRing)
        ++this->m_stats.jumpTouches;

    if (this->m_log.size() < EbOrbStackResolver::kMaxTouchLog) {
        this->m_log.push_back(ev);
    }

    if (EbStageConfig::debugLogging()) {
        EbAssistLog::debug(fmt::format("orb-stack: touch oid={} uid={} toggle={} jumpRing={} "
                                     "jumpHeld={} claimed={} appliedJump={} tick={}",
                                     ev.oid, ev.uid, toggle ? 1 : 0, jumpRing ? 1 : 0,
                                     jumpHeld ? 1 : 0, claimed ? 1 : 0, appliedJump ? 1 : 0,
                                     simTick));
    }
}

int EbOrbStackResolver::fireNormalOrbsAt(PlayLayer* layer, PlayerObject* shadow,
                                       GameObject* toggleHit) {
    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return 0;

    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    if (!shadow->m_holdingButtons[jumpIdx])
        return 0;

    int applied = 0;
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!object || object == toggleHit)
            continue;
        if (!EbOrbStackResolver::isJumpOrDashRing(object))
            continue;
        if (!EbOrbStackResolver::withinColocateEps(object, toggleHit))
            continue;

        auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object);
        if (!ring)
            continue;

        // Already clicked this run — vanilla second call / prior partner fire.
        if (ring->m_claimTouch || ring->m_activated ||
            shadow->m_touchedRings.count(ring->m_uniqueID) > 0) {
            continue;
        }

        EbShadowSimulator::get().applyRingJump(shadow, ring);
        if (ring->m_claimTouch || shadow->m_touchedRings.count(ring->m_uniqueID) > 0) {
            ++applied;
            ++this->m_stats.stackDualFires;
            this->noteTouch(shadow, ring, true, EbShadowSimulator::get().simAbsTick());
            if (EbStageConfig::debugLogging()) {
                EbAssistLog::debug(
                    fmt::format("orb-stack: dual-fire jump oid={} uid={} with-toggle={}",
                                ring->m_objectID, ring->m_uniqueID, toggleHit->m_uniqueID));
            }
        }
    }
    return applied;
}

int EbOrbStackResolver::fireTogglesAt(PlayLayer* layer, PlayerObject* shadow, GameObject* jumpHit) {
    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return 0;

    int applied = 0;
    auto& toggleSim = EbToggleOrbSim::get();
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!object || object == jumpHit)
            continue;
        if (!EbTriggerClassify::isToggleOrbId(object->m_objectID))
            continue;
        if (!EbOrbStackResolver::withinColocateEps(object, jumpHit))
            continue;

        // ToggleOrbSim claims effects — skip if already handled this run.
        if (toggleSim.isModeled(object->m_uniqueID))
            continue;
        auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object);
        if (ring && (ring->m_claimTouch || ring->m_activated))
            continue;

        toggleSim.onToggleHit(layer, shadow, object);
        // Count dual-fire even when the group had 0 flip members (orb still claimed).
        if (!toggleSim.isModeled(object->m_uniqueID) && !(ring && ring->m_claimTouch)) {
            continue;
        }
        ++applied;
        ++this->m_stats.stackDualFires;
        if (ring) {
            this->noteTouch(shadow, ring, false, EbShadowSimulator::get().simAbsTick());
        }
        if (EbStageConfig::debugLogging()) {
            EbAssistLog::debug(fmt::format("orb-stack: dual-fire toggle oid={} uid={} with-jump={}",
                                         object->m_objectID, object->m_uniqueID,
                                         jumpHit->m_uniqueID));
        }
    }
    return applied;
}

int EbOrbStackResolver::fireStackedPartners(PlayLayer* layer, PlayerObject* shadow, GameObject* hit) {
    if (!layer || !shadow || !hit)
        return 0;

    bool const toggle = EbTriggerClassify::isToggleOrbId(hit->m_objectID);
    bool const touchToggle = [&]() {
        auto* effect = geode::prelude::typeinfo_cast<EffectGameObject*>(hit);
        return effect && EbTriggerClassify::isToggleTriggerId(hit->m_objectID) &&
               effect->m_isTouchTriggered;
    }();
    bool const jumpRing = EbOrbStackResolver::isJumpOrDashRing(hit);

    if (toggle || touchToggle) {
        return this->fireNormalOrbsAt(layer, shadow, hit);
    }
    if (jumpRing) {
        return this->fireTogglesAt(layer, shadow, hit);
    }
    return 0;
}

bool EbOrbStackResolver::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    expect(EbOrbStackResolver::isJumpOrDashRingType(GameObjectType::YellowJumpRing), "yellow-jump");
    expect(EbOrbStackResolver::isJumpOrDashRingType(GameObjectType::DashRing), "dash");
    expect(EbOrbStackResolver::isJumpOrDashRingType(GameObjectType::PinkJumpRing), "pink-jump");
    expect(!EbOrbStackResolver::isJumpOrDashRingType(GameObjectType::Solid), "solid-not-ring");

    expect(!EbOrbStackResolver::isJumpOrDashRing(nullptr), "null-ring");
    expect(!EbOrbStackResolver::shouldSkipToggleFlip(nullptr, nullptr), "null-skip");
    expect(!EbOrbStackResolver::withinColocateEps(nullptr, nullptr), "null-eps");
    expect(EbOrbStackResolver::get().fireStackedPartners(nullptr, nullptr, nullptr) == 0,
           "null-fire");

    auto& res = EbOrbStackResolver::get();
    res.begin();
    expect(res.stats().touchCount == 0, "begin-clear");
    expect(res.touchLog().empty(), "begin-log");
    expect(std::fabs(EbOrbStackResolver::kColocateEps - 12.f) < 1e-6f, "eps-12");

    // Contract: toggle id is never classified as jump/dash partner.
    expect(!EbTriggerClassify::isToggleOrbId(36), "yellow-oid-not-toggle");
    expect(EbTriggerClassify::isToggleOrbId(1594), "toggle-oid");

    res.begin();
    if (fails == 0) {
        report = "OrbStackResolver selfCheck OK";
        return true;
    }
    report = fmt::format("OrbStackResolver selfCheck {} fails: {}", fails, report);
    return false;
}
