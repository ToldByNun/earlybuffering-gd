#include "shadow_physics.hpp"
#include "../../assist/assist_clock.hpp"
#include "../../diagnostics/sim_tick_cost_meter.hpp"
#include "../shadow_simulator.hpp"
#include <chrono>

void EbShadowPhysics::markDead(EbShadowSimulator& simulator, PlayerObject* player, GameObject* object) {
    // Active sim body (live shadow or eval ghost) drives m_dead.
    if (player == simulator.m_shadow ||
        (simulator.m_evalActive && player == simulator.m_evalActive)) {
        simulator.m_dead = true;
        simulator.m_deathObject = object;
    }
    // Sim death is tracked on the simulator — never leave the ghost's m_isDead
    // latched. Mega Hack / QOL "hitboxes on death" poll PlayerObject::m_isDead
    // (or latch on destroyPlayer); a dead-looking shadow would flash overlays.
    if (player && simulator.isOwnedGhost(player)) {
        player->m_isDead = false;
        simulator.noteGhostDeathHitboxSuppress();
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

    if (simulator.m_collectOrbContacts)
        simulator.m_orbContacts.push_back(ring);

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
    // Ghost muted once in copyPlayerState / beginSession — not every collide.
    simulator.clearCollisionLogs(state);
    layer->checkCollisions(state, gdDt, false);
    // Vanilla may latch m_isDead before destroyPlayer; keep ghosts "alive"
    // for hitbox-on-death menus while simulator.m_dead holds the truth.
    if (state && simulator.isOwnedGhost(state) && state->m_isDead)
        state->m_isDead = false;
}

void EbShadowPhysics::stepPhysics(EbShadowSimulator& simulator, PlayLayer* layer,
                                  PlayerObject* state, float stepDt, float deltaTime,
                                  bool splitPrefix, cocos2d::CCPoint const* tickStart) {
    auto const tickWallStart = std::chrono::steady_clock::now();
    if (simulator.m_collectOrbContacts)
        simulator.m_orbContacts.clear();
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
    float const gdColDt = splitPrefix ? 0.f : EbAssistClock::secondsToGdStep(colDt);
    // Ghost muted once in copyPlayerState / beginSession — not 3× per physics tick.
    state->update(gdStepDt);
    if (tickStart)
        state->m_lastPosition = *tickStart;
    if (!simulator.m_dead) {
        simulator.clearCollisionLogs(state);
        float const collisionDelta =
            splitPrefix && state->m_isOnSlope ? EbAssistClock::secondsToGdStep(deltaTime) : gdColDt;
        layer->checkCollisions(state, collisionDelta, splitPrefix);
    }
    if (state && simulator.isOwnedGhost(state) && state->m_isDead)
        state->m_isDead = false;
    // AP1 measure-only: feed Adaptive BudgetController (does not change search).
    int const us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - tickWallStart)
                                        .count());
    EbSimTickCostMeter::get().notePhysicsTick(us);
}
