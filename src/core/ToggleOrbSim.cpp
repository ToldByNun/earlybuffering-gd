#include "ToggleOrbSim.hpp"
#include "TriggerClassify.hpp"
#include "../debug/AssistLog.hpp"

#include <fmt/format.h>

using namespace geode::prelude;

ToggleOrbSim& ToggleOrbSim::get() {
    static ToggleOrbSim s;
    return s;
}

void ToggleOrbSim::applyCollisionActive(GameObject* obj, bool active) {
    if (!obj) return;
    auto* ego = typeinfo_cast<EnhancedGameObject*>(obj);
    if (active) {
        if (ego) {
            ego->m_poweredOn = true;
            ego->m_visible = true;
        }
        obj->setVisible(true);
    }
    else {
        // Inactive must not collide: visibility alone left solids deadly (Commit 4).
        if (ego) {
            ego->m_poweredOn = false;
            ego->m_visible = false;
        }
        obj->setVisible(false);
    }
}

void ToggleOrbSim::captureCollision(GameObject* obj, CollisionSnap& out) {
    out = {};
    if (!obj) return;
    out.obj = obj;
    out.uniqueId = obj->m_uniqueID;
    out.isActivated = obj->m_isActivated;
    out.nodeVisible = obj->isVisible();
    out.opacity = obj->getOpacity();
    if (auto* ego = typeinfo_cast<EnhancedGameObject*>(obj)) {
        out.poweredOn = ego->m_poweredOn;
        out.activated = ego->m_activated;
        out.egoVisible = ego->m_visible;
    }
    if (auto* ring = typeinfo_cast<RingObject*>(obj)) {
        out.isRing = true;
        out.claimTouch = ring->m_claimTouch;
    }
}

void ToggleOrbSim::restoreCollision(CollisionSnap const& snap) {
    if (!snap.obj) return;
    snap.obj->m_isActivated = snap.isActivated;
    if (auto* ego = typeinfo_cast<EnhancedGameObject*>(snap.obj)) {
        ego->m_poweredOn = snap.poweredOn;
        ego->m_activated = snap.activated;
        ego->m_visible = snap.egoVisible;
    }
    snap.obj->setOpacity(snap.opacity);
    snap.obj->setVisible(snap.nodeVisible);
    if (snap.isRing) {
        if (auto* ring = typeinfo_cast<RingObject*>(snap.obj)) {
            ring->m_claimTouch = snap.claimTouch;
        }
    }
}

void ToggleOrbSim::begin() {
    // Drop tracking only — callers must restore() first if snaps are dirty.
    this->m_snaps.clear();
    this->m_modeledActive.clear();
    this->m_claimedEffects.clear();
}

void ToggleOrbSim::restore() {
    for (auto it = this->m_snaps.rbegin(); it != this->m_snaps.rend(); ++it) {
        ToggleOrbSim::restoreCollision(*it);
    }
    this->begin();
}

bool ToggleOrbSim::isModeled(int uniqueId) const {
    return this->m_modeledActive.find(uniqueId) != this->m_modeledActive.end();
}

bool ToggleOrbSim::isModeledActive(int uniqueId) const {
    auto it = this->m_modeledActive.find(uniqueId);
    if (it == this->m_modeledActive.end()) return false;
    return it->second;
}

bool ToggleOrbSim::onToggleHit(
    PlayLayer* layer, PlayerObject* shadow, GameObject* hit
) {
    if (!layer || !shadow || !hit) return false;
    if (this->m_claimedEffects.size() >= kMaxClaimedEffects) return false;

    int const effectId = hit->m_uniqueID;
    if (this->m_claimedEffects.count(effectId)) return false;

    int const oid = hit->m_objectID;
    bool const toggleOrb = TriggerClassify::isToggleOrbId(oid);

    // Effect fields (group / activate / touch) live on EffectGameObject.
    auto* effect = typeinfo_cast<EffectGameObject*>(hit);
    bool const touchToggle = effect
        && TriggerClassify::isToggleTriggerId(oid)
        && effect->m_isTouchTriggered;
    if (!toggleOrb && !touchToggle) return false;

    if (!effect || effect->m_targetGroupID <= 0) {
        // Cannot model without group — claim to avoid re-entry; stay Unknown.
        this->m_claimedEffects.insert(effectId);
        if (auto* ring = typeinfo_cast<RingObject*>(hit)) {
            if (!this->isModeled(effectId)) {
                CollisionSnap orbSnap;
                ToggleOrbSim::captureCollision(ring, orbSnap);
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
    if (!children) return false;

    size_t flipped = 0;
    for (unsigned i = 0; i < children->count(); ++i) {
        if (flipped >= kMaxFlipMembers) break;
        if (this->m_snaps.size() >= kMaxFlipMembers) break;

        auto* obj = typeinfo_cast<GameObject*>(children->objectAtIndex(i));
        if (!obj) continue;
        if (typeinfo_cast<PlayerObject*>(obj)) continue;
        if (obj == hit) continue;
        if (!obj->belongsToGroup(groupId)) continue;

        // Already modeled this run (multi-effect same group) — keep last wantActive.
        if (this->isModeled(obj->m_uniqueID)) continue;

        CollisionSnap snap;
        ToggleOrbSim::captureCollision(obj, snap);

        bool wantActive = false;
        if (toggleOrb) {
            // Toggle orb: invert current collision participation.
            auto* ego = typeinfo_cast<EnhancedGameObject*>(obj);
            bool const currentlyOn = obj->isVisible()
                && (!ego || ego->m_poweredOn);
            wantActive = !currentlyOn;
        }
        else {
            // Touch toggle 1049: absolute activateGroup.
            wantActive = activateGroup;
        }
        snap.wantActive = wantActive;

        ToggleOrbSim::applyCollisionActive(obj, wantActive);
        this->m_snaps.push_back(snap);
        this->m_modeledActive[obj->m_uniqueID] = wantActive;
        ++flipped;
    }

    this->m_claimedEffects.insert(effectId);

    // Claim the effect object itself so ring/trigger hooks do not re-enter.
    if (auto* ring = typeinfo_cast<RingObject*>(hit)) {
        if (!this->isModeled(effectId)) {
            CollisionSnap orbSnap;
            ToggleOrbSim::captureCollision(ring, orbSnap);
            orbSnap.wantActive = true;
            this->m_snaps.push_back(orbSnap);
            this->m_modeledActive[effectId] = true;
        }
        ring->m_claimTouch = true;
        ring->m_activated = true;
        ring->m_isActivated = true;
    }

    if (flipped > 0) {
        AssistLog::debug(fmt::format(
            "toggle-orb-sim: flip group={} members={} orb={} touch={} effect={}",
            groupId,
            flipped,
            toggleOrb ? 1 : 0,
            touchToggle ? 1 : 0,
            effectId
        ));
    }
    return flipped > 0;
}

bool ToggleOrbSim::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // Null-safe helpers.
    ToggleOrbSim::applyCollisionActive(nullptr, false);
    CollisionSnap nullSnap;
    ToggleOrbSim::captureCollision(nullptr, nullSnap);
    ToggleOrbSim::restoreCollision(nullSnap);
    expect(nullSnap.obj == nullptr, "null-capture");

    auto& sim = ToggleOrbSim::get();
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
