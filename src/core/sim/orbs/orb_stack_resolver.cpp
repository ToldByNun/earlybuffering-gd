#include "orb_stack_resolver.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../trigger_world/trigger_classify.hpp"
#include "../foresight/travel_foresight.hpp"
#include "../shadow_simulator.hpp"
#include "toggle_orb_sim.hpp"
#include <cmath>
#include <fmt/format.h>

OrbStackResolver& OrbStackResolver::get() {
    static OrbStackResolver s;
    return s;
}

void OrbStackResolver::begin() {
    this->m_stats = {};
    this->m_log.clear();
}

bool OrbStackResolver::isJumpOrDashRingType(GameObjectType type) {
    return TravelForesight::isOrbLike(type);
}

bool OrbStackResolver::isJumpOrDashRing(GameObject* object) {
    if (!object)
        return false;
    if (!(TriggerClassify::isToggleOrbId(object->m_objectID))) {
        return OrbStackResolver::isJumpOrDashRingType(object->m_objectType);
    }
    return false;
}

bool OrbStackResolver::withinColocateEps(GameObject* firstValue, GameObject* secondValue) {
    if (!firstValue || !secondValue)
        return false;
    float const deltaX = firstValue->getPositionX() - secondValue->getPositionX();
    float const deltaY = firstValue->getPositionY() - secondValue->getPositionY();
    return (deltaX * deltaX + deltaY * deltaY) <=
           (OrbStackResolver::kColocateEps * OrbStackResolver::kColocateEps);
}

bool OrbStackResolver::shouldSkipToggleFlip(GameObject* member, GameObject* toggleHit) {
    if (!member)
        return false;
    if (OrbStackResolver::isJumpOrDashRing(member))
        return true;
    if (toggleHit && geode::prelude::typeinfo_cast<RingObject*>(member) &&
        !TriggerClassify::isToggleOrbId(member->m_objectID) &&
        OrbStackResolver::withinColocateEps(member, toggleHit)) {
        return true;
    }
    return false;
}

void OrbStackResolver::noteFlipSkipped() {
    ++this->m_stats.flipSkipped;
}

void OrbStackResolver::noteTouch(PlayerObject* shadow, RingObject* ring, bool appliedJump,
                                 int simTick) {
    if (!ring)
        return;

    bool const toggle = TriggerClassify::isToggleOrbId(ring->m_objectID);
    bool const jumpRing = OrbStackResolver::isJumpOrDashRing(ring);
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

    if (this->m_log.size() < OrbStackResolver::kMaxTouchLog) {
        this->m_log.push_back(ev);
    }

    if (StageConfig::debugLogging()) {
        AssistLog::debug(fmt::format("orb-stack: touch oid={} uid={} toggle={} jumpRing={} "
                                     "jumpHeld={} claimed={} appliedJump={} tick={}",
                                     ev.oid, ev.uid, toggle ? 1 : 0, jumpRing ? 1 : 0,
                                     jumpHeld ? 1 : 0, claimed ? 1 : 0, appliedJump ? 1 : 0,
                                     simTick));
    }
}

int OrbStackResolver::fireNormalOrbsAt(PlayLayer* layer, PlayerObject* shadow,
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
        if (!OrbStackResolver::isJumpOrDashRing(object))
            continue;
        if (!OrbStackResolver::withinColocateEps(object, toggleHit))
            continue;

        auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object);
        if (!ring)
            continue;

        // Already clicked this run — vanilla second call / prior partner fire.
        if (ring->m_claimTouch || ring->m_activated ||
            shadow->m_touchedRings.count(ring->m_uniqueID) > 0) {
            continue;
        }

        ShadowSimulator::get().applyRingJump(shadow, ring);
        if (ring->m_claimTouch || shadow->m_touchedRings.count(ring->m_uniqueID) > 0) {
            ++applied;
            ++this->m_stats.stackDualFires;
            this->noteTouch(shadow, ring, true, ShadowSimulator::get().simAbsTick());
            if (StageConfig::debugLogging()) {
                AssistLog::debug(
                    fmt::format("orb-stack: dual-fire jump oid={} uid={} with-toggle={}",
                                ring->m_objectID, ring->m_uniqueID, toggleHit->m_uniqueID));
            }
        }
    }
    return applied;
}

int OrbStackResolver::fireTogglesAt(PlayLayer* layer, PlayerObject* shadow, GameObject* jumpHit) {
    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return 0;

    int applied = 0;
    auto& toggleSim = ToggleOrbSim::get();
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!object || object == jumpHit)
            continue;
        if (!TriggerClassify::isToggleOrbId(object->m_objectID))
            continue;
        if (!OrbStackResolver::withinColocateEps(object, jumpHit))
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
            this->noteTouch(shadow, ring, false, ShadowSimulator::get().simAbsTick());
        }
        if (StageConfig::debugLogging()) {
            AssistLog::debug(fmt::format("orb-stack: dual-fire toggle oid={} uid={} with-jump={}",
                                         object->m_objectID, object->m_uniqueID,
                                         jumpHit->m_uniqueID));
        }
    }
    return applied;
}

int OrbStackResolver::fireStackedPartners(PlayLayer* layer, PlayerObject* shadow, GameObject* hit) {
    if (!layer || !shadow || !hit)
        return 0;

    bool const toggle = TriggerClassify::isToggleOrbId(hit->m_objectID);
    bool const touchToggle = [&]() {
        auto* effect = geode::prelude::typeinfo_cast<EffectGameObject*>(hit);
        return effect && TriggerClassify::isToggleTriggerId(hit->m_objectID) &&
               effect->m_isTouchTriggered;
    }();
    bool const jumpRing = OrbStackResolver::isJumpOrDashRing(hit);

    if (toggle || touchToggle) {
        return this->fireNormalOrbsAt(layer, shadow, hit);
    }
    if (jumpRing) {
        return this->fireTogglesAt(layer, shadow, hit);
    }
    return 0;
}

bool OrbStackResolver::selfCheck(std::string& report) {
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

    expect(OrbStackResolver::isJumpOrDashRingType(GameObjectType::YellowJumpRing), "yellow-jump");
    expect(OrbStackResolver::isJumpOrDashRingType(GameObjectType::DashRing), "dash");
    expect(OrbStackResolver::isJumpOrDashRingType(GameObjectType::PinkJumpRing), "pink-jump");
    expect(!OrbStackResolver::isJumpOrDashRingType(GameObjectType::Solid), "solid-not-ring");

    expect(!OrbStackResolver::isJumpOrDashRing(nullptr), "null-ring");
    expect(!OrbStackResolver::shouldSkipToggleFlip(nullptr, nullptr), "null-skip");
    expect(!OrbStackResolver::withinColocateEps(nullptr, nullptr), "null-eps");
    expect(OrbStackResolver::get().fireStackedPartners(nullptr, nullptr, nullptr) == 0,
           "null-fire");

    auto& res = OrbStackResolver::get();
    res.begin();
    expect(res.stats().touchCount == 0, "begin-clear");
    expect(res.touchLog().empty(), "begin-log");
    expect(std::fabs(OrbStackResolver::kColocateEps - 12.f) < 1e-6f, "eps-12");

    // Contract: toggle id is never classified as jump/dash partner.
    expect(!TriggerClassify::isToggleOrbId(36), "yellow-oid-not-toggle");
    expect(TriggerClassify::isToggleOrbId(1594), "toggle-oid");

    res.begin();
    if (fails == 0) {
        report = "OrbStackResolver selfCheck OK";
        return true;
    }
    report = fmt::format("OrbStackResolver selfCheck {} fails: {}", fails, report);
    return false;
}
