#include "../../../app/class_manager.hpp"
#include "toggle_orb_sim.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../trigger_world/trigger_classify.hpp"
#include "orb_stack_resolver.hpp"
#include <fmt/format.h>

EbToggleOrbSim& EbToggleOrbSim::get() {
    return EbClassManager::get().service<EbToggleOrbSim>();
}

void EbToggleOrbSim::applyCollisionActive(GameObject* object, bool active) {
    if (!object)
        return;
    auto* effectObject = geode::prelude::typeinfo_cast<EnhancedGameObject*>(object);
    if (active) {
        if (effectObject) {
            effectObject->m_poweredOn = true;
            effectObject->m_visible = true;
        }
        object->setVisible(true);
    } else {
        // Inactive must not collide: visibility alone left solids deadly (Commit 4).
        if (effectObject) {
            effectObject->m_poweredOn = false;
            effectObject->m_visible = false;
        }
        object->setVisible(false);
    }
}

void EbToggleOrbSim::captureCollision(GameObject* object, CollisionSnap& out) {
    out = {};
    if (!object)
        return;
    out.obj = object;
    out.uniqueId = object->m_uniqueID;
    out.isActivated = object->m_isActivated;
    out.nodeVisible = object->isVisible();
    out.opacity = object->getOpacity();
    if (auto* ego = geode::prelude::typeinfo_cast<EnhancedGameObject*>(object)) {
        out.poweredOn = ego->m_poweredOn;
        out.activated = ego->m_activated;
        out.egoVisible = ego->m_visible;
    }
    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object)) {
        out.isRing = true;
        out.claimTouch = ring->m_claimTouch;
    }
}

void EbToggleOrbSim::restoreCollision(CollisionSnap const& snap) {
    if (!snap.obj)
        return;
    snap.obj->m_isActivated = snap.isActivated;
    if (auto* ego = geode::prelude::typeinfo_cast<EnhancedGameObject*>(snap.obj)) {
        ego->m_poweredOn = snap.poweredOn;
        ego->m_activated = snap.activated;
        ego->m_visible = snap.egoVisible;
    }
    snap.obj->setOpacity(snap.opacity);
    snap.obj->setVisible(snap.nodeVisible);
    if (snap.isRing) {
        if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(snap.obj)) {
            ring->m_claimTouch = snap.claimTouch;
        }
    }
}

void EbToggleOrbSim::begin() {
    // Drop tracking only — callers must restore() first if snaps are dirty.
    this->m_snaps.clear();
    this->m_modeledActive.clear();
    this->m_claimedEffects.clear();
}

void EbToggleOrbSim::restore() {
    for (auto it = this->m_snaps.rbegin(); it != this->m_snaps.rend(); ++it) {
        EbToggleOrbSim::restoreCollision(*it);
    }
    this->begin();
}

bool EbToggleOrbSim::isModeled(int uniqueId) const {
    return this->m_modeledActive.find(uniqueId) != this->m_modeledActive.end();
}

bool EbToggleOrbSim::isModeledActive(int uniqueId) const {
    auto it = this->m_modeledActive.find(uniqueId);
    if (!(it == this->m_modeledActive.end())) {
        return it->second;
    }
    return false;
}

bool EbToggleOrbSim::onToggleHit(PlayLayer* layer, PlayerObject* shadow, GameObject* hit) {
    if (!layer || !shadow || !hit)
        return false;
    if (this->m_claimedEffects.size() >= kMaxClaimedEffects)
        return false;

    int const effectId = hit->m_uniqueID;
    if (this->m_claimedEffects.count(effectId))
        return false;

    int const oid = hit->m_objectID;
    bool const toggleOrb = EbTriggerClassify::isToggleOrbId(oid);

    // Effect fields (group / activate / touch) live on EffectGameObject.
    auto* effect = geode::prelude::typeinfo_cast<EffectGameObject*>(hit);
    bool const touchToggle =
        effect && EbTriggerClassify::isToggleTriggerId(oid) && effect->m_isTouchTriggered;
    if (!toggleOrb && !touchToggle)
        return false;

    if (!effect || effect->m_targetGroupID <= 0) {
        // Cannot model without group — claim to avoid re-entry; stay Unknown.
        this->m_claimedEffects.insert(effectId);
        if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(hit)) {
            if (!this->isModeled(effectId)) {
                CollisionSnap orbSnap;
                EbToggleOrbSim::captureCollision(ring, orbSnap);
                this->m_snaps.push_back(orbSnap);
                this->m_modeledActive[effectId] = true;
            }
            ring->m_claimTouch = true;
            ring->m_activated = true;
            ring->m_isActivated = true;
        }
        return false;
    }

    int const groupId = effect->m_targetGroupID;
    bool const activateGroup = effect->m_activateGroup;

    auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
    if (!children)
        return false;

    size_t flipped = 0;
    for (unsigned index = 0; index < children->count(); ++index) {
        if (flipped >= kMaxFlipMembers)
            break;
        if (this->m_snaps.size() >= kMaxFlipMembers)
            break;

        auto* object = geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
        if (!object)
            continue;
        if (geode::prelude::typeinfo_cast<PlayerObject*>(object))
            continue;
        if (object == hit)
            continue;
        if (!object->belongsToGroup(groupId))
            continue;

        // Commit 7 (a): never flip jump/dash rings (or ε-near rings) — stack keeps boost.
        if (EbOrbStackResolver::shouldSkipToggleFlip(object, hit)) {
            EbOrbStackResolver::get().noteFlipSkipped();
            continue;
        }

        // Already modeled this run (multi-effect same group) — keep last wantActive.
        if (this->isModeled(object->m_uniqueID))
            continue;

        CollisionSnap snap;
        EbToggleOrbSim::captureCollision(object, snap);

        bool wantActive = false;
        if (toggleOrb) {
            // Toggle orb: invert current collision participation.
            auto* effectObject = geode::prelude::typeinfo_cast<EnhancedGameObject*>(object);
            bool const currentlyOn =
                object->isVisible() && (!effectObject || effectObject->m_poweredOn);
            wantActive = !currentlyOn;
        } else {
            // Touch toggle 1049: absolute activateGroup.
            wantActive = activateGroup;
        }
        snap.wantActive = wantActive;

        EbToggleOrbSim::applyCollisionActive(object, wantActive);
        this->m_snaps.push_back(snap);
        this->m_modeledActive[object->m_uniqueID] = wantActive;
        ++flipped;
    }

    this->m_claimedEffects.insert(effectId);

    // Claim the effect object itself so ring/trigger hooks do not re-enter.
    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(hit)) {
        if (!this->isModeled(effectId)) {
            CollisionSnap orbSnap;
            EbToggleOrbSim::captureCollision(ring, orbSnap);
            orbSnap.wantActive = true;
            this->m_snaps.push_back(orbSnap);
            this->m_modeledActive[effectId] = true;
        }
        ring->m_claimTouch = true;
        ring->m_activated = true;
        ring->m_isActivated = true;
    }

    if (flipped > 0) {
        EbAssistLog::debug(
            fmt::format("toggle-orb-sim: flip group={} members={} orb={} touch={} effect={}",
                        groupId, flipped, toggleOrb ? 1 : 0, touchToggle ? 1 : 0, effectId));
    }
    return flipped > 0;
}

bool EbToggleOrbSim::selfCheck(std::string& report) {
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

    // Null-safe helpers.
    EbToggleOrbSim::applyCollisionActive(nullptr, false);
    CollisionSnap nullSnap;
    EbToggleOrbSim::captureCollision(nullptr, nullSnap);
    EbToggleOrbSim::restoreCollision(nullSnap);
    expect(nullSnap.obj == nullptr, "null-capture");

    auto& sim = EbToggleOrbSim::get();
    sim.restore();
    expect(!sim.isModeled(1), "fresh-not-modeled");
    expect(!sim.isModeledActive(1), "fresh-not-active");

    // Logical flip+restore bookkeeping without a live GameObject.
    CollisionSnap synthetic{};
    synthetic.uniqueId = 99;
    synthetic.poweredOn = true;
    synthetic.nodeVisible = true;
    synthetic.egoVisible = true;
    synthetic.wantActive = false;
    sim.m_snaps.push_back(synthetic);
    sim.m_modeledActive[99] = false;
    expect(sim.isModeled(99), "syn-modeled");
    expect(!sim.isModeledActive(99), "syn-inactive");
    sim.restore();
    expect(!sim.isModeled(99), "syn-restored");
    expect(sim.m_snaps.empty(), "syn-snaps-clear");

    // Claim idempotence bookkeeping.
    sim.m_claimedEffects.insert(7);
    expect(sim.m_claimedEffects.count(7) == 1, "claim-once");
    sim.begin();
    expect(sim.m_claimedEffects.empty(), "claim-cleared");

    if (fails == 0) {
        report = "ToggleOrbSim selfCheck OK";
        return true;
    }
    report = fmt::format("ToggleOrbSim selfCheck {} fails: {}", fails, report);
    return false;
}
