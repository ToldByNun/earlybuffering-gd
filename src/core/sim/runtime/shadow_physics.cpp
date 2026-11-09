#include "shadow_physics.hpp"
#include "../../assist/assist_clock.hpp"
#include "../shadow_simulator.hpp"

void EbShadowPhysics::markDead(EbShadowSimulator& simulator, PlayerObject* player, GameObject* object) {
    // Active sim body (live shadow or eval ghost) drives m_dead.
    if (player == simulator.m_shadow ||
        (simulator.m_evalActive && player == simulator.m_evalActive)) {
        simulator.m_dead = true;
        simulator.m_deathObject = object;
    }
}

void EbShadowPhysics::noteJump(EbShadowSimulator& simulator) {
    simulator.m_jumped = true;
}

void EbShadowPhysics::noteActivated(EbShadowSimulator& simulator, GameObject* gobj) {
    auto* object = geode::prelude::typeinfo_cast<EnhancedGameObject*>(gobj);
    if (!object)
        return;
    for (auto const& s : simulator.m_saved) {
        if (s.obj == object)
            return;
    }
    EbShadowSimulator::SavedObject s;
    s.obj = object;
    s.activated = object->m_activated;
    s.p1 = object->m_activatedByPlayer1;
    s.p2 = object->m_activatedByPlayer2;
    s.poweredOn = object->m_poweredOn;
    s.state = object->m_state;
    s.isActivated = object->m_isActivated;
    s.egoVisible = object->m_visible;
    s.opacity = object->getOpacity();
    s.nodeVisible = object->isVisible();
    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object)) {
        s.isRing = true;
        s.claimTouch = ring->m_claimTouch;
    }
    simulator.m_saved.push_back(s);
}

void EbShadowPhysics::noteOrbOverlap(EbShadowSimulator& simulator) {
    simulator.m_orbOverlapTick = true;
}

void EbShadowPhysics::applyRingJump(EbShadowSimulator& simulator, PlayerObject* shadow,
                                  RingObject* ring) {
    if (!shadow || !ring || (shadow != simulator.m_shadow && shadow != simulator.m_evalActive))
        return;

    // Real GD: orbs only fire while Jump is held. Without this, wait-ticks before a
    // delayed press claim the orb early and every orb prediction is wrong.
    int const jumpIdx = static_cast<int>(PlayerButton::Jump);
    bool const holding = shadow->m_holdingButtons[jumpIdx];
    if (!holding) {
        simulator.m_orbOverlapTick = true;
        return;
    }

    // Already used this run (and not multi-activate) → one jump only.
    bool const multi = ring->m_isMultiActivate || ring->canAllowMultiActivate();
    if (!multi && (ring->m_claimTouch || ring->m_activated))
        return;
    if (!multi && shadow->m_touchedRings.count(ring->m_uniqueID))
        return;

    simulator.noteActivated(ring);
    simulator.m_activatedRing = true;
    ++simulator.m_ringCount;

    // Real orb physics. Re-entrancy flag lets the ringJump hook call the original.
    // VFX hooks (spawnCircle, activateObject visuals, particles) stay blocked.
    // RAII so a throw / early exit never leaves m_applyingRing stuck (iOS live path).
    class EbApplyingRingGuard {
    public:
        bool& flag;
        explicit EbApplyingRingGuard(bool& f) : flag(f) {
            flag = true;
        }
        ~EbApplyingRingGuard() {
            flag = false;
        }

    private:
    };
    EbApplyingRingGuard applyingGuard(simulator.m_applyingRing);
    shadow->ringJump(ring, false);

    // Claim for the rest of this run so collision doesn't re-boost every tick.
    // restoreCapture() puts the real orb back afterward.
    if (!multi) {
        ring->m_claimTouch = true;
        ring->m_activated = true;
        ring->m_isActivated = true;
        if (shadow->m_isSecondPlayer)
            ring->m_activatedByPlayer2 = true;
        else
            ring->m_activatedByPlayer1 = true;
        shadow->m_touchedRings.insert(ring->m_uniqueID);
    }
    shadow->m_touchedRing = true;
    shadow->m_hasEverHitRing = true;
}

void EbShadowPhysics::stepPreCollide(EbShadowSimulator& simulator, PlayLayer* layer,
                                   PlayerObject* state, float deltaTime) {
    // Always use the full step dt here — tiny CBF remainders launch slopes
    // and also under-detect orb/spike overlaps.
    simulator.m_triggerWorldAppliedThisTick = false;
    simulator.m_liveDynamicAppliedIds.clear();
    if (simulator.m_simAbsTick >= 0) {
        simulator.applyTriggerWorldAtTick(layer, simulator.m_simAbsTick, state);
        simulator.applyLiveDynamicNear(layer, state);
    }
    float const gdDt = EbAssistClock::secondsToGdStep(deltaTime);
    simulator.hideGhost(state);
    simulator.clearCollisionLogs(state);
    layer->checkCollisions(state, gdDt, false);
    simulator.hideGhost(state);
}

void EbShadowPhysics::stepPhysics(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* state,
                                float stepDt, float deltaTime) {
    simulator.m_triggerWorldAppliedThisTick = false;
    simulator.m_liveDynamicAppliedIds.clear();
    if (simulator.m_simAbsTick >= 0) {
        simulator.applyTriggerWorldAtTick(layer, simulator.m_simAbsTick, state);
        simulator.applyLiveDynamicNear(layer, state);
    }
    // v1.2.1 contract: ghost must see the same GD step units as live
    // PlayerObject::update (0.25 @ 240 TPS). stepDt/dt are AssistClock seconds.
    float const gdStepDt = EbAssistClock::secondsToGdStep(stepDt);
    float colDt = stepDt;
    if (colDt < deltaTime)
        colDt = deltaTime; // same rule as pre-collide: never a tiny remainder
    float const gdColDt = EbAssistClock::secondsToGdStep(colDt);
    simulator.hideGhost(state);
    state->update(gdStepDt);
    simulator.hideGhost(state);
    if (simulator.m_dead)
        return;
    simulator.clearCollisionLogs(state);
    layer->checkCollisions(state, gdColDt, false);
    simulator.hideGhost(state);
}
