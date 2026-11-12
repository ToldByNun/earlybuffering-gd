#include "shadow_freeze_evaluator.hpp"
#include "../../../debug/lag_probe.hpp"
#include "../../../debug/perf_trace.hpp"
#include "../../../dev/capture/dev_clock_contract.hpp"
#include "../../../dev/capture/dev_live_witness.hpp"
#include "../../../dev/evaluation/dev_eval_restore.hpp"
#include "../../trigger_world/trigger_world_store.hpp"
#include "../shadow_simulator.hpp"

void EbShadowFreezeEvaluator::applyPlayerBlob(PlayerObject* destination, EbDevPlayerBlob const& blob) {
    if (!destination || !blob.present)
        return;
    destination->setPosition({blob.x, blob.y});
    destination->m_position = cocos2d::CCPoint{blob.x, blob.y};
    destination->setRotation(blob.rotation);
    destination->setScaleX(blob.scaleX);
    destination->setScaleY(blob.scaleY);
    destination->m_yVelocity = blob.yVelocity;
    destination->m_fallSpeed = blob.fallSpeed;
    destination->m_gravity = blob.gravity;
    destination->m_gravityMod = blob.gravityMod;
    destination->m_speedMultiplier = blob.speedMultiplier;
    destination->m_playerSpeed = blob.playerSpeed;
    destination->m_vehicleSize = blob.vehicleSize;
    destination->m_platformerXVelocity = blob.platformerXVelocity;
    destination->m_isUpsideDown = blob.isUpsideDown;
    destination->m_isSideways = blob.isSideways;
    destination->m_isGoingLeft = blob.isGoingLeft;
    destination->m_isShip = blob.isShip;
    destination->m_isBall = blob.isBall;
    destination->m_isBird = blob.isBird;
    destination->m_isSwing = blob.isSwing;
    destination->m_isDart = blob.isDart;
    destination->m_isRobot = blob.isRobot;
    destination->m_isSpider = blob.isSpider;
    destination->m_isSecondPlayer = blob.isSecondPlayer;
    destination->m_isPlatformer = blob.isPlatformer;
    destination->m_isDashing = blob.isDashing;
    destination->m_isOnGround = blob.isOnGround;
    destination->m_isOnGround2 = blob.isOnGround2;
    destination->m_isOnSlope = blob.isOnSlope;
    destination->m_wasOnSlope = blob.wasOnSlope;
    destination->m_jumpBuffered = blob.jumpBuffered;
    destination->m_wasJumpBuffered = blob.wasJumpBuffered;
    destination->m_stateRingJump = blob.stateRingJump;
    destination->m_hasEverJumped = blob.hasEverJumped;
    destination->m_hasEverHitRing = blob.hasEverHitRing;
    destination->m_isLocked = blob.isLocked;
    destination->m_controlsDisabled = blob.controlsDisabled;
    destination->m_maybeIsFalling = blob.maybeIsFalling;
    destination->m_maybeIsBoosted = blob.maybeIsBoosted;
    destination->m_isDead = false;
    EbShadowSimulator::hideGhost(destination);
}

void EbShadowFreezeEvaluator::applyEvalWorldRestore(EbShadowSimulator& simulator, PlayLayer* layer,
                                                  EbDevLiveWitness const& witness,
                                                  EbDevEvalRestore& restore) {
    if (!layer)
        return;

    // Snapshot live activated-ID map + clear per-run object flag saves.
    simulator.m_saved.clear();
    simulator.m_savedTransforms.clear();
    simulator.m_savedActivatedIDs = layer->m_gameState.m_activatedObjectIDs;

    // --- Dual ghost ---
    if (witness.dual.present && simulator.m_evalGhosts[1]) {
        simulator.applyPlayerBlob(simulator.m_evalGhosts[1], witness.dual);
        EbDevEvalRestore::noteDualWritten(restore, witness.dual);
    }

    // --- Orbs: set activated ID set + mark matching objects claimed ---
    {
        bool const wantOrbs = EbDevLiveWitness::hasWorldDep(witness, DevWitnessCategory::Orbs);
        if (wantOrbs || !witness.orbIds.empty()) {
            layer->m_gameState.m_activatedObjectIDs.clear();
            auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
            for (int uid : witness.orbIds) {
                if (uid == 0)
                    continue;
                std::pair<int, int> const activatedKey{uid, 0};
                layer->m_gameState.m_activatedObjectIDs[activatedKey] = 1;
                if (!children)
                    continue;
                for (unsigned index = 0; index < children->count(); ++index) {
                    auto* object = geode::prelude::typeinfo_cast<EnhancedGameObject*>(
                        children->objectAtIndex(index));
                    if (!object || object->m_uniqueID != uid)
                        continue;
                    simulator.noteActivated(object);
                    object->m_activated = true;
                    object->m_isActivated = true;
                    if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object)) {
                        ring->m_claimTouch = true;
                    }
                    break;
                }
            }
            EbDevEvalRestore::noteOrbsWritten(restore, witness.orbIds);
        }
    }

    // --- Triggers: spool store poses at witness apply tick ---
    if (witness.triggerCanSpool) {
        simulator.captureTriggerTransforms(layer);
        int const tick =
            witness.triggerApplyTick >= 0 ? witness.triggerApplyTick : witness.clock.effectTick;
        EbTriggerWorldStore::get().applyAtTick(layer, tick);
        EbDevEvalRestore::noteTriggersSpooled(restore, witness.triggerStoreKey,
                                            witness.triggerStoreVersion, tick);
    }

    // --- Movers: freeze-world poses, else digest-only ---
    if (witness.freezeWorld) {
        auto* children = layer->m_objectLayer ? layer->m_objectLayer->getChildren() : nullptr;
        if (children) {
            for (auto const& pose : witness.moverPoses) {
                GameObject* object = nullptr;
                for (unsigned index = 0; index < children->count(); ++index) {
                    auto* candidate =
                        geode::prelude::typeinfo_cast<GameObject*>(children->objectAtIndex(index));
                    if (!candidate)
                        continue;
                    if (candidate->m_uniqueID != pose.uniqueId)
                        continue;
                    object = candidate;
                    break;
                }
                if (!object)
                    continue;

                bool already = false;
                for (auto const& st : simulator.m_savedTransforms) {
                    if (st.uniqueId == pose.uniqueId) {
                        already = true;
                        break;
                    }
                }
                if (!already) {
                    EbShadowSimulator::SavedTransform st;
                    st.uniqueId = object->m_uniqueID;
                    st.obj = object;
                    st.x = object->getPositionX();
                    st.y = object->getPositionY();
                    st.rotation = object->getRotation();
                    st.scaleX = object->getScaleX();
                    st.scaleY = object->getScaleY();
                    st.nodeVisible = object->isVisible();
                    simulator.m_savedTransforms.push_back(st);
                }

                EbTriggerPose tp;
                tp.tick = 0;
                tp.x = pose.x;
                tp.y = pose.y;
                tp.rotation = pose.rotation;
                tp.scaleX = pose.scaleX;
                tp.scaleY = pose.scaleY;
                tp.active = pose.active;
                EbTriggerWorldStore::writePose(object, tp);
            }
        }
        EbDevEvalRestore::noteMoversPosesWritten(restore, witness.moverPoses,
                                               witness.hasHazardDigest ? witness.hazardDigest : 0);
    } else if (witness.hasHazardDigest) {
        EbDevEvalRestore::noteMoversDigestOnly(restore, witness.hazardDigest);
    }
}

void EbShadowFreezeEvaluator::finishEvalWorldRestore(EbShadowSimulator& simulator, PlayLayer* layer) {
    if (!layer) {
        simulator.m_saved.clear();
        simulator.m_savedTransforms.clear();
        simulator.m_savedActivatedIDs.clear();
        return;
    }
    {
        EbLagProbe::Scope const scope("eb.trigger.restore");
        simulator.restoreTriggerTransforms(layer);
    }
    for (auto const& s : simulator.m_saved) {
        if (!s.obj)
            continue;
        s.obj->m_activated = s.activated;
        s.obj->m_activatedByPlayer1 = s.p1;
        s.obj->m_activatedByPlayer2 = s.p2;
        s.obj->m_poweredOn = s.poweredOn;
        s.obj->m_state = s.state;
        s.obj->m_isActivated = s.isActivated;
        s.obj->m_visible = s.egoVisible;
        s.obj->setOpacity(s.opacity);
        s.obj->setVisible(s.nodeVisible);
        if (s.isRing) {
            static_cast<RingObject*>(s.obj)->m_claimTouch = s.claimTouch;
        }
    }
    simulator.m_saved.clear();
    layer->m_gameState.m_activatedObjectIDs = simulator.m_savedActivatedIDs;
    simulator.m_savedActivatedIDs.clear();
}

EbShadowSimulator::EvalFromFreezeResult
EbShadowFreezeEvaluator::evalDelayFromFreeze(EbShadowSimulator& simulator, PlayLayer* layer,
                                           EbDevLiveWitness const& witness, int delay,
                                           int horizonTicks, bool down, int* tickBudgetLeft,
                                           bool captureTraj) {
    EbShadowSimulator::EvalFromFreezeResult out;
    out.evalSimPathId = DevSimPathId::EvalDelayFromFreeze;
    simulator.m_evalAbort = false;

    if (!witness.valid || !witness.clock.valid || !EbDevClockContract::get().clockOk(witness.clock)) {
        out.clockMismatch = true;
        return out;
    }
    if (!layer || !simulator.m_evalGhosts[0] || layer != simulator.m_hostLayer) {
        return out;
    }
    if (!witness.player.present) {
        return out;
    }
    if (delay < 0)
        delay = 0;
    if (horizonTicks < 0)
        horizonTicks = 0;

    auto& clock = EbDevClockContract::get();
    auto steps = clock.stepSchedule(witness.clock, delay, horizonTicks);
    out.stepScheduleHash = clock.hashSteps(steps);
    if (steps.empty())
        return out;

    out.restore = EbDevEvalRestore::beginFromWitness(witness, DevSimPathId::EvalDelayFromFreeze);

    PlayerObject* ghost = simulator.m_evalGhosts[0];
    simulator.applyPlayerBlob(ghost, witness.player);
    EbDevEvalRestore::notePlayerWritten(out.restore, witness.player);
    simulator.applyEvalWorldRestore(layer, witness, out.restore);

    simulator.m_evalActive = ghost;
    simulator.m_simulating = true;
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;
    simulator.m_simTickBase = witness.clock.effectTick;

    ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    bool const tapRelease =
        !ghost->m_isDart && !ghost->m_isShip && !ghost->m_isSwing && !ghost->m_isRobot;
    bool acted = false;
    bool tapReleased = false;
    out.actionTick = delay;
    out.ran = true;

    auto spend = [&]() -> bool {
        if (!tickBudgetLeft)
            return false;
        if (*tickBudgetLeft <= 0)
            return true;
        --(*tickBudgetLeft);
        return false;
    };

    auto pushTraj = [&](int tick) {
        if (!captureTraj)
            return;
        EbShadowSimulator::TrajSample sample;
        sample.tick = tick;
        sample.x = ghost->getPositionX();
        sample.y = ghost->getPositionY();
        sample.vy = static_cast<float>(ghost->m_yVelocity);
        sample.alive = !simulator.m_dead;
        sample.unknown = simulator.m_unknown;
        out.traj.push_back(sample);
    };

    if (delay > 0) {
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase;
        simulator.m_triggerWorldAppliedThisTick = false;
        simulator.stepPreCollide(layer, ghost, witness.clock.simDt);
        if (simulator.m_dead) {
            out.ticksRan = 1;
            out.died = true;
            out.deathTick = 0;
            out.endX = ghost->getPositionX();
            pushTraj(0);
        } else if (simulator.m_unknown) {
            out.ticksRan = 1;
            out.unknown = true;
            out.endX = ghost->getPositionX();
            pushTraj(0);
        }
    }

    int const countValue = static_cast<int>(steps.size());
    for (int tick = 0; tick < countValue && !out.died && !out.unknown; ++tick) {
        if (simulator.m_evalAbort) {
            out.aborted = true;
            break;
        }
        if (spend()) {
            out.budgetHit = true;
            break;
        }
        float const stepDt = steps[static_cast<size_t>(tick)];
        out.ticksRan = tick + 1;
        simulator.m_orbOverlapTick = false;
        simulator.m_simAbsTick = simulator.m_simTickBase + tick;
        simulator.m_triggerWorldAppliedThisTick = false;

        if (!acted && tick == delay) {
            acted = true;
            if (down) {
                ghost->pushButton(PlayerButton::Jump);
                ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = true;
            } else {
                ghost->releaseButton(PlayerButton::Jump);
                ghost->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = false;
            }
        } else if (acted && down && tapRelease && !tapReleased && simulator.m_jumped) {
            bool const touchingOrb =
                simulator.m_orbOverlapTick ||
                (ghost->m_touchingRings && ghost->m_touchingRings->count() > 0);
            bool const dashing = ghost->m_isDashing;
            bool const justHitOrb = simulator.m_activatedRing && tick <= delay + 4;
            if (!touchingOrb && !dashing && !justHitOrb) {
                tapReleased = true;
                ghost->releaseButton(PlayerButton::Jump);
            }
        }

        simulator.stepPhysics(layer, ghost, stepDt, witness.clock.simDt);
        pushTraj(tick);
        if (simulator.m_dead) {
            out.died = true;
            out.deathTick = tick;
            out.endX = ghost->getPositionX();
            break;
        }
        if (simulator.m_unknown) {
            out.unknown = true;
            out.endX = ghost->getPositionX();
            break;
        }
        out.endX = ghost->getPositionX();
    }

    simulator.m_simAbsTick = -1;
    simulator.m_simulating = false;
    simulator.m_evalActive = nullptr;
    simulator.finishEvalWorldRestore(layer);
    // Park eval ghost (same evacuate rules as shadow).
    if (auto* pl = PlayLayer::get()) {
        if (pl == simulator.m_hostLayer) {
            simulator.tryRemoveFromSection(pl, ghost);
        }
    }
    ghost->m_outerSectionIndex = -1;
    ghost->m_innerSectionIndex = -1;
    ghost->m_middleSectionIndex = -1;
    ghost->setPosition({0.f, 105.f});
    ghost->m_position = cocos2d::CCPoint{0.f, 105.f};
    simulator.hideGhost(ghost);
    if (simulator.m_evalGhosts[1]) {
        simulator.hideGhost(simulator.m_evalGhosts[1]);
        simulator.m_evalGhosts[1]->setPosition({0.f, 105.f});
        simulator.m_evalGhosts[1]->m_position = cocos2d::CCPoint{0.f, 105.f};
    }
    return out;
}

EbShadowSimulator::EvalFromFreezeResult
EbShadowFreezeEvaluator::replayActualFire(EbShadowSimulator& simulator, PlayLayer* layer,
                                        EbDevLiveWitness const& witness, int actualFireTickAbs,
                                        int horizonTicks, bool down, int* tickBudgetLeft) {
    EbShadowSimulator::EvalFromFreezeResult out;
    out.evalSimPathId = DevSimPathId::ReplayActualFire;
    simulator.m_evalAbort = false;

    if (!witness.valid || !witness.clock.valid || !EbDevClockContract::get().clockOk(witness.clock)) {
        out.clockMismatch = true;
        return out;
    }
    // Delay from effectTick to actual fire (clamped ≥ 0).
    int delay = actualFireTickAbs - witness.clock.effectTick;
    if (delay < 0)
        delay = 0;

    // Reuse the schedule path but retag evalSimPathId / restore path id.
    out = simulator.evalDelayFromFreeze(layer, witness, delay, horizonTicks, down, tickBudgetLeft);
    out.evalSimPathId = DevSimPathId::ReplayActualFire;
    if (out.restore.valid) {
        out.restore.evalSimPathId = DevSimPathId::ReplayActualFire;
    }
    return out;
}
