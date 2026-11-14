#include "shadow_candidate_runner.hpp"
#include "../planning/ball_landing_tracker.hpp"
#include "../planning/orb_continuation.hpp"
#include "../planning/simulation_plan.hpp"
#include "../shadow_simulator.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

EbShadowSimulator::RunResult
EbShadowCandidateRunner::runOne(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                                bool down, int delay, int limit, float deltaTime, float firstDt,
                                EbShadowSimulator::Model const& model, int releaseAfter,
                                EbThreatZone const* zone, int* tickBudgetLeft, float firePhase) {
    EbShadowSimulator::RunResult r;
    if (!std::isfinite(firePhase) || firePhase < 0.f || firePhase >= 1.f) {
        r.unknown = r.unknownTrunc = true;
        return r;
    }
    (void)limit;
    simulator.copyPlayerState(simulator.m_shadow, real);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    int groundedStreak = 0;
    int settledAfterClear = 0;
    bool pastZone = false;
    EbOrbContinuation orbContinuation;
    std::unordered_set<int> firstOrbIds;
    bool capturedFirstOrbs = false;
    bool const probeOrbContinuation = model.probeOrbContinuation && real->m_isBird && down;
    simulator.m_collectOrbContacts = probeOrbContinuation;
    simulator.m_orbContacts.clear();
    EbBallLandingTracker ballLanding;
    ballLanding.reset(shadowPlayer->m_isUpsideDown);
    int const maxTicks = std::max(std::max(0, limit), delay) + std::max(1, model.cap);

    auto finish = [&](EbShadowSimulator::RunResult output) {
        simulator.m_collectOrbContacts = false;
        simulator.m_orbContacts.clear();
        output.activatedRing = simulator.m_activatedRing;
        output.ringCount = simulator.m_ringCount;
        if (probeOrbContinuation && orbContinuation.attempted()) {
            bool const verified = !output.died && !output.unknown && !output.unknownTrunc &&
                                  !simulator.m_toggleUnknownHit && output.ticksRan >= model.cap &&
                                  orbContinuation.verified(output.ringCount, output.ticksRan);
            if (verified)
                output.orbContinuationTick = orbContinuation.pressTick();
            else
                output.unknownTrunc = true;
        }
        output.endX = shadowPlayer->getPositionX();
        output.wasDashing = shadowPlayer->m_isDashing;
        output.toggleUnknown = simulator.m_toggleUnknownHit;
        if (output.toggleUnknown)
            output.unknownTrunc = true;
        output.ticksRan =
            std::max(output.ticksRan, output.deathTick >= 0 ? output.deathTick + 1 : 0);
        if (output.ticksBeforeNeutral <= 0)
            output.ticksBeforeNeutral = output.ticksRan;
        return output;
    };

    auto spend = [&](int tickIndex) -> bool {
        return simulator.spendPlanTick(tickBudgetLeft, tickIndex);
    };

    if (delay > 0) {
        if (simulator.markPlanWallIfExceeded())
            return finish(r);
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase;
        simulator.stepPreCollide(layer, shadowPlayer, deltaTime);
        if (simulator.m_dead) {
            r.ticksRan = 1;
            r.died = true;
            r.deathTick = 0;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = simulator.m_deathObject;
            r.diedBeforeAction = true;
            return finish(r);
        }
        if (simulator.m_unknown) {
            r.ticksRan = 1;
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = 0;
            return finish(r);
        }
    }

    for (int tick = 0; tick < maxTicks; ++tick) {
        if (spend(tick))
            break;
        float stepDt = (tick == 0) ? firstDt : deltaTime;
        cocos2d::CCPoint tickStart;
        bool splitAction = false;
        r.ticksRan = tick + 1;
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase + tick;

        if (!acted && tick == delay) {
            if (firePhase > 0.f) {
                tickStart = shadowPlayer->getPosition();
                float const prefix = stepDt * firePhase;
                simulator.stepPhysics(layer, shadowPlayer, prefix, deltaTime, true);
                if (simulator.m_dead || simulator.m_unknown) {
                    r.died = simulator.m_dead;
                    r.unknown = simulator.m_unknown;
                    r.unknownTrunc = simulator.m_unknown;
                    r.diedBeforeAction = simulator.m_dead;
                    r.deathTick = simulator.m_dead ? tick : -1;
                    r.deathX = shadowPlayer->getPositionX();
                    r.deathObject = simulator.m_deathObject;
                    return finish(r);
                }
                simulator.clearCollisionLogs(shadowPlayer);
                stepDt -= prefix;
                splitAction = true;
            }
            acted = true;
            if (down) {
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && down && real->m_isSpider && model.probeSpiderContinuation &&
                   releaseAfter > 1) {
            if (tick == delay + 1) {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
                tapReleased = true;
            } else if (tick == delay + releaseAfter) {
                simulator.m_jumped = false;
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else if (tick == delay + releaseAfter + 1) {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && !down && real->m_isDart && releaseAfter >= 0 && !tapReleased &&
                   tick == delay + releaseAfter) {
            // Recovery probe only: a possible next player press, never injected.
            tapReleased = true;
            shadowPlayer->pushButton(PlayerButton::Jump);
            shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
        } else if (acted && down) {
            if (releaseAfter >= 0 && !tapReleased && tick == delay + releaseAfter) {
                tapReleased = true;
                shadowPlayer->releaseButton(PlayerButton::Jump);
            } else if (releaseAfter < 0 && model.tapRelease && !tapReleased && simulator.m_jumped) {
                bool const touchingOrb =
                    simulator.m_orbOverlapTick ||
                    (shadowPlayer->m_touchingRings && shadowPlayer->m_touchingRings->count() > 0);
                bool const dashing = shadowPlayer->m_isDashing;
                bool const justHitOrb = simulator.m_activatedRing && tick <= delay + 4;
                if (!touchingOrb && !dashing && !justHitOrb) {
                    tapReleased = true;
                    shadowPlayer->releaseButton(PlayerButton::Jump);
                }
            }
        }

        if (probeOrbContinuation && acted && simulator.m_ringCount > 0 &&
            !shadowPlayer->m_isDashing) {
            bool freshOrb = false;
            if (!capturedFirstOrbs) {
                capturedFirstOrbs = true;
                for (auto* ring : simulator.m_orbContacts) {
                    if (ring)
                        firstOrbIds.insert(ring->m_uniqueID);
                }
            }
            // Ring collisions are intercepted during simulation; native
            // m_touchingRings is therefore not a reliable ghost contact list.
            if (!firstOrbIds.empty()) {
                for (auto* ring : simulator.m_orbContacts) {
                    if (ring && !firstOrbIds.contains(ring->m_uniqueID) &&
                        !shadowPlayer->m_touchedRings.count(ring->m_uniqueID) &&
                        !ring->m_claimTouch && !ring->m_activated) {
                        freshOrb = true;
                        break;
                    }
                }
            }
            auto const input = orbContinuation.next(tick, simulator.m_ringCount, freshOrb);
            if (input == EbOrbContinuation::Input::Release) {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
                tapReleased = true;
            } else if (input == EbOrbContinuation::Input::Press) {
                // Possible next PLAYER input, never scheduled into the real game.
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            }
        }

        simulator.stepPhysics(layer, shadowPlayer, stepDt, deltaTime, false,
                              splitAction ? &tickStart : nullptr);
        if (simulator.m_dead) {
            r.died = true;
            r.deathTick = tick;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = simulator.m_deathObject;
            r.diedBeforeAction = !acted;
            return finish(r);
        }
        if (simulator.m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = tick;
            return finish(r);
        }

        if (model.probeSpiderContinuation && real->m_isSpider && releaseAfter > 1 &&
            tick >= delay + releaseAfter && simulator.m_jumped)
            r.followupJumped = true;

        if (model.stopAtBallLanding && acted && shadowPlayer->m_isBall) {
            bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
            if (ballLanding.observe(shadowPlayer->m_isUpsideDown, grounded)) {
                r.landedAfterAction = true;
                return finish(r);
            }
        }

        if (simulator.m_jumped && !r.jumped) {
            r.jumped = true;
            r.jumpTick = tick;
        }
        if (shadowPlayer->m_isDashing)
            r.wasDashing = true;

        // G2: after threat zone + settled → neutral (stop extending survive score)
        if (zone && zone->valid && !pastZone) {
            if (shadowPlayer->getPositionX() > zone->endX) {
                bool settled = false;
                if (model.continuous) {
                    settledAfterClear++;
                    settled = settledAfterClear >= 8;
                } else {
                    bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
                    if (grounded)
                        ++groundedStreak;
                    else
                        groundedStreak = 0;
                    settled = groundedStreak >= 2;
                }
                if (settled) {
                    pastZone = true;
                    r.zoneCleared = true;
                    r.ticksBeforeNeutral = tick + 1;
                    // Keep simulating briefly for orb/path info but mark neutral
                }
            } else {
                settledAfterClear = 0;
            }
        }

        if (pastZone && !model.continuePastClear && tick + 1 - r.ticksBeforeNeutral > 24) {
            return finish(r);
        }
    }
    return finish(r);
}

EbShadowSimulator::RunResult EbShadowCandidateRunner::runTrajectoryOne(
    EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real, bool down, int delay,
    int limit, float deltaTime, float firstDt, EbShadowSimulator::Model const& model,
    int releaseAfter, EbThreatZone const* zone, int* tickBudgetLeft) {
    // Lighter Live V2 path: same button timing as runOne, no delay-0 orb pre-scan.
    EbShadowSimulator::RunResult r;
    (void)limit;
    if (!layer || !real || !simulator.m_shadow)
        return r;
    simulator.copyPlayerState(simulator.m_shadow, real);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool acted = false;
    bool tapReleased = false;
    int groundedStreak = 0;
    int settledAfterClear = 0;
    bool pastZone = false;
    int const maxTicks = std::max(std::max(0, limit), delay) + std::max(1, model.cap);

    auto finish = [&](EbShadowSimulator::RunResult output) {
        output.activatedRing = simulator.m_activatedRing;
        output.ringCount = simulator.m_ringCount;
        output.endX = shadowPlayer->getPositionX();
        output.wasDashing = shadowPlayer->m_isDashing;
        output.toggleUnknown = simulator.m_toggleUnknownHit;
        if (output.toggleUnknown)
            output.unknownTrunc = true;
        output.ticksRan =
            std::max(output.ticksRan, output.deathTick >= 0 ? output.deathTick + 1 : 0);
        if (output.ticksBeforeNeutral <= 0)
            output.ticksBeforeNeutral = output.ticksRan;
        return output;
    };

    auto spend = [&](int tickIndex) -> bool {
        return simulator.spendPlanTick(tickBudgetLeft, tickIndex);
    };

    for (int tick = 0; tick < maxTicks; ++tick) {
        if (spend(tick))
            break;
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        r.ticksRan = tick + 1;
        simulator.m_simAbsTick = simulator.m_simTickBase + tick;

        if (!acted && tick == delay) {
            acted = true;
            if (down) {
                shadowPlayer->pushButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else {
                shadowPlayer->releaseButton(PlayerButton::Jump);
                shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && down && releaseAfter >= 0 && !tapReleased &&
                   tick == delay + releaseAfter) {
            tapReleased = true;
            shadowPlayer->releaseButton(PlayerButton::Jump);
        }

        simulator.stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        if (simulator.m_dead) {
            r.died = true;
            r.deathTick = tick;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = simulator.m_deathObject;
            r.diedBeforeAction = !acted;
            return finish(r);
        }
        if (simulator.m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = tick;
            return finish(r);
        }

        if (simulator.m_jumped && !r.jumped) {
            r.jumped = true;
            r.jumpTick = tick;
        }
        if (shadowPlayer->m_isDashing)
            r.wasDashing = true;

        if (zone && zone->valid && !pastZone) {
            if (shadowPlayer->getPositionX() > zone->endX) {
                bool settled = false;
                if (model.continuous) {
                    settledAfterClear++;
                    settled = settledAfterClear >= 8;
                } else {
                    bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
                    if (grounded)
                        ++groundedStreak;
                    else
                        groundedStreak = 0;
                    settled = groundedStreak >= 2;
                }
                if (settled) {
                    pastZone = true;
                    r.zoneCleared = true;
                    r.ticksBeforeNeutral = tick + 1;
                }
            } else {
                settledAfterClear = 0;
            }
        }

        if (pastZone && !model.continuePastClear && tick + 1 - r.ticksBeforeNeutral > 24) {
            return finish(r);
        }
    }
    return finish(r);
}

EbShadowSimulator::RunResult
EbShadowCandidateRunner::runBaseline(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* real,
                                   bool down, float deltaTime, float firstDt,
                                   EbShadowSimulator::Model const& model, int* tickBudgetLeft) {
    EbShadowSimulator::RunResult r;
    simulator.copyPlayerState(simulator.m_shadow, real);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    int const maxTicks = std::max(1, model.cap);
    for (int tick = 0; tick < maxTicks; ++tick) {
        if (simulator.spendPlanTick(tickBudgetLeft, tick))
            break;
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        simulator.m_simAbsTick = simulator.m_simTickBase + tick;
        simulator.stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        r.ticksRan = tick + 1;
        r.endX = shadowPlayer->getPositionX();
        if (simulator.m_dead) {
            r.died = true;
            r.deathTick = tick;
            r.deathX = shadowPlayer->getPositionX();
            r.deathObject = simulator.m_deathObject;
            return r;
        }
        if (simulator.m_unknown) {
            r.unknown = true;
            r.unknownTrunc = true;
            r.unknownTick = tick;
            return r;
        }
    }
    return r;
}

EbShadowSimulator::OrbWindow
EbShadowCandidateRunner::scanOrbWindow(EbShadowSimulator& simulator, PlayLayer* layer,
                                     PlayerObject* real, bool down, int scanTicks, float deltaTime,
                                     float firstDt, int* tickBudgetLeft, bool holdJump) {
    EbShadowSimulator::OrbWindow win;
    if (!layer || !real || !simulator.m_shadow || scanTicks <= 0)
        return win;

    simulator.copyPlayerState(simulator.m_shadow, real);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_orbOverlapTick = false;
    simulator.m_deathObject = nullptr;

    auto* shadowPlayer = simulator.m_shadow;
    // Press-to-reach: hold Jump. Otherwise legacy coast with Jump = !down.
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = holdJump ? true : !down;

    simulator.m_orbScanning = true;
    for (int tick = 0; tick < scanTicks; ++tick) {
        if (simulator.spendPlanTick(tickBudgetLeft, tick))
            break;
        float const stepDt = (tick == 0) ? firstDt : deltaTime;
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase + tick;

        simulator.stepPreCollide(layer, shadowPlayer, deltaTime);
        bool const touchPre =
            simulator.m_orbOverlapTick ||
            (shadowPlayer->m_touchingRings && shadowPlayer->m_touchingRings->count() > 0);
        if (touchPre) {
            if (!win.found) {
                win.found = true;
                win.first = tick;
            }
            win.last = tick;
        }
        if (simulator.m_dead || simulator.m_unknown)
            break;

        simulator.m_orbOverlapTick = false;
        simulator.stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
        bool const touchPost =
            simulator.m_orbOverlapTick ||
            (shadowPlayer->m_touchingRings && shadowPlayer->m_touchingRings->count() > 0);
        if (touchPost) {
            if (!win.found) {
                win.found = true;
                win.first = tick;
            }
            win.last = tick;
        }
        if (simulator.m_dead || simulator.m_unknown)
            break;
    }
    simulator.m_orbScanning = false;
    return win;
}
