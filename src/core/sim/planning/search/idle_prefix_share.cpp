#include "idle_prefix_share.hpp"
#include "../../../../debug/assist_log.hpp"
#include "../../../assist/contracts/live_plan_contracts.hpp"
#include "../../../assist/contracts/miss_assist_contracts.hpp"
#include "../../orbs/toggle_orb_sim.hpp"
#include "../../prefix_sim.hpp"
#include "../../shadow_simulator.hpp"
#include "../shadow_survival_rules.hpp"
#include "../simulation_candidate.hpp"
#include "shadow_plan_context.hpp"
#include <algorithm>
#include <fmt/format.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EbIdlePrefixForkState {
public:
    class LiveObjectFlags {
    public:
        bool activated = false;
        bool p1 = false;
        bool p2 = false;
        bool poweredOn = false;
        int state = 0;
        bool claimTouch = false;
        bool isRing = false;
        bool isActivated = false;
        bool egoVisible = true;
        GLubyte opacity = 255;
        bool nodeVisible = true;

    private:
    };

    class TransformPose {
    public:
        float x = 0.f;
        float y = 0.f;
        float rotation = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        bool nodeVisible = true;
        bool hasAppliedPose = false;
        float appliedX = 0.f;
        float appliedY = 0.f;
        float appliedRotation = 0.f;
        float appliedScaleX = 1.f;
        float appliedScaleY = 1.f;
        bool appliedActive = true;

    private:
    };

    class ToggleMidState {
    public:
        std::vector<EbToggleOrbSim::CollisionSnap> snaps;
        std::unordered_map<int, bool> modeledActive;
        std::unordered_set<int> claimedEffects;

    private:
    };

    class ForkPoint {
    public:
        std::vector<EbShadowSimulator::SavedObject> saved;
        std::vector<LiveObjectFlags> live;
        std::vector<TransformPose> transforms;
        gd::map<std::pair<int, int>, int> activatedIDs;
        ToggleMidState toggle;
        int simAbsTick = -1;
        bool activatedRing = false;
        int ringCount = 0;
        bool jumped = false;
        bool unknown = false;
        bool toggleUnknownHit = false;
        bool orbOverlapTick = false;

    private:
    };

    static LiveObjectFlags captureLiveFlags(EnhancedGameObject* object, bool isRing,
                                            bool claimTouch) {
        LiveObjectFlags live;
        if (!object)
            return live;
        live.activated = object->m_activated;
        live.p1 = object->m_activatedByPlayer1;
        live.p2 = object->m_activatedByPlayer2;
        live.poweredOn = object->m_poweredOn;
        live.state = object->m_state;
        live.isActivated = object->m_isActivated;
        live.egoVisible = object->m_visible;
        live.opacity = object->getOpacity();
        live.nodeVisible = object->isVisible();
        live.isRing = isRing;
        live.claimTouch = claimTouch;
        return live;
    }

    static void applyLiveFlags(EnhancedGameObject* object, LiveObjectFlags const& live) {
        if (!object)
            return;
        object->m_activated = live.activated;
        object->m_activatedByPlayer1 = live.p1;
        object->m_activatedByPlayer2 = live.p2;
        object->m_poweredOn = live.poweredOn;
        object->m_state = live.state;
        object->m_isActivated = live.isActivated;
        object->m_visible = live.egoVisible;
        object->setOpacity(live.opacity);
        object->setVisible(live.nodeVisible);
        if (live.isRing) {
            if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(object))
                ring->m_claimTouch = live.claimTouch;
        }
    }

    static bool captureFork(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* forkGhost,
                            ForkPoint& out) {
        if (!forkGhost || !simulator.m_shadow)
            return false;
        EbShadowSimulator::copyPlayerState(forkGhost, simulator.m_shadow);
        out.saved = simulator.m_saved;
        out.live.clear();
        out.live.reserve(out.saved.size());
        for (auto const& saved : out.saved) {
            bool claim = false;
            if (saved.isRing && saved.obj) {
                if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(saved.obj))
                    claim = ring->m_claimTouch;
            }
            out.live.push_back(captureLiveFlags(saved.obj, saved.isRing, claim));
        }
        out.transforms.clear();
        out.transforms.reserve(simulator.m_savedTransforms.size());
        for (auto const& transform : simulator.m_savedTransforms) {
            TransformPose pose;
            if (transform.obj) {
                pose.x = transform.obj->getPositionX();
                pose.y = transform.obj->getPositionY();
                pose.rotation = transform.obj->getRotation();
                pose.scaleX = transform.obj->getScaleX();
                pose.scaleY = transform.obj->getScaleY();
                pose.nodeVisible = transform.obj->isVisible();
            }
            pose.hasAppliedPose = transform.hasAppliedPose;
            pose.appliedX = transform.appliedX;
            pose.appliedY = transform.appliedY;
            pose.appliedRotation = transform.appliedRotation;
            pose.appliedScaleX = transform.appliedScaleX;
            pose.appliedScaleY = transform.appliedScaleY;
            pose.appliedActive = transform.appliedActive;
            out.transforms.push_back(pose);
        }
        if (layer)
            out.activatedIDs = layer->m_gameState.m_activatedObjectIDs;
        auto& toggle = EbToggleOrbSim::get();
        out.toggle.snaps = toggle.m_snaps;
        out.toggle.modeledActive = toggle.m_modeledActive;
        out.toggle.claimedEffects = toggle.m_claimedEffects;
        out.simAbsTick = simulator.m_simAbsTick;
        out.activatedRing = simulator.m_activatedRing;
        out.ringCount = simulator.m_ringCount;
        out.jumped = simulator.m_jumped;
        out.unknown = simulator.m_unknown;
        out.toggleUnknownHit = simulator.m_toggleUnknownHit;
        out.orbOverlapTick = simulator.m_orbOverlapTick;
        return true;
    }

    static bool restoreFork(EbShadowSimulator& simulator, PlayLayer* layer, PlayerObject* forkGhost,
                            ForkPoint const& point) {
        if (!forkGhost || !simulator.m_shadow)
            return false;

        EbToggleOrbSim::get().restore();
        simulator.resetTriggerTransforms();
        for (auto const& saved : simulator.m_saved) {
            if (!saved.obj)
                continue;
            saved.obj->m_activated = saved.activated;
            saved.obj->m_activatedByPlayer1 = saved.p1;
            saved.obj->m_activatedByPlayer2 = saved.p2;
            saved.obj->m_poweredOn = saved.poweredOn;
            saved.obj->m_state = saved.state;
            saved.obj->m_isActivated = saved.isActivated;
            saved.obj->m_visible = saved.egoVisible;
            saved.obj->setOpacity(saved.opacity);
            saved.obj->setVisible(saved.nodeVisible);
            if (saved.isRing) {
                if (auto* ring = geode::prelude::typeinfo_cast<RingObject*>(saved.obj))
                    ring->m_claimTouch = saved.claimTouch;
            }
        }
        simulator.m_saved.clear();
        if (layer)
            layer->m_gameState.m_activatedObjectIDs = simulator.m_savedActivatedIDs;

        auto& toggle = EbToggleOrbSim::get();
        toggle.m_snaps = point.toggle.snaps;
        toggle.m_modeledActive = point.toggle.modeledActive;
        toggle.m_claimedEffects = point.toggle.claimedEffects;
        for (auto const& snap : toggle.m_snaps) {
            auto found = toggle.m_modeledActive.find(snap.uniqueId);
            if (found != toggle.m_modeledActive.end() && snap.obj)
                EbToggleOrbSim::applyCollisionActive(snap.obj, found->second);
        }

        simulator.m_saved = point.saved;
        size_t const n = std::min(simulator.m_saved.size(), point.live.size());
        for (size_t index = 0; index < n; ++index)
            applyLiveFlags(simulator.m_saved[index].obj, point.live[index]);

        size_t const tn = std::min(simulator.m_savedTransforms.size(), point.transforms.size());
        for (size_t index = 0; index < tn; ++index) {
            auto& transform = simulator.m_savedTransforms[index];
            auto const& pose = point.transforms[index];
            if (transform.obj) {
                transform.obj->setPosition({pose.x, pose.y});
                transform.obj->setRotation(pose.rotation);
                transform.obj->setScaleX(pose.scaleX);
                transform.obj->setScaleY(pose.scaleY);
                transform.obj->setVisible(pose.nodeVisible);
            }
            transform.hasAppliedPose = pose.hasAppliedPose;
            transform.appliedX = pose.appliedX;
            transform.appliedY = pose.appliedY;
            transform.appliedRotation = pose.appliedRotation;
            transform.appliedScaleX = pose.appliedScaleX;
            transform.appliedScaleY = pose.appliedScaleY;
            transform.appliedActive = pose.appliedActive;
        }
        if (layer)
            layer->m_gameState.m_activatedObjectIDs = point.activatedIDs;

        EbShadowSimulator::copyPlayerState(simulator.m_shadow, forkGhost);
        simulator.m_simAbsTick = point.simAbsTick;
        simulator.m_activatedRing = point.activatedRing;
        simulator.m_ringCount = point.ringCount;
        simulator.m_jumped = point.jumped;
        simulator.m_dead = false;
        simulator.m_unknown = point.unknown;
        simulator.m_toggleUnknownHit = point.toggleUnknownHit;
        simulator.m_orbOverlapTick = point.orbOverlapTick;
        simulator.m_deathObject = nullptr;
        return true;
    }

    static EbShadowSimulator::RunResult runPressContinuation(
        EbShadowSimulator& simulator, PlayLayer* layer, bool down, int delay, int limit,
        float deltaTime, float firstDt, EbShadowSimulator::Model const& model, int releaseAfter,
        EbThreatZone const* zone, int* tickBudgetLeft) {
        EbShadowSimulator::RunResult r;
        (void)limit;
        auto* shadowPlayer = simulator.m_shadow;
        if (!shadowPlayer)
            return r;

        bool acted = false;
        bool tapReleased = false;
        int groundedStreak = 0;
        int settledAfterClear = 0;
        bool pastZone = false;
        bool const initialGravity = shadowPlayer->m_isUpsideDown;
        bool ballFlipped = false;
        bool ballAirborne = false;
        int ballLandingTicks = 0;
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

        auto spend = [&]() -> bool {
            if (!tickBudgetLeft)
                return false;
            if (*tickBudgetLeft <= 0)
                return true;
            --(*tickBudgetLeft);
            return false;
        };

        for (int tick = delay; tick < maxTicks; ++tick) {
            if (spend())
                break;
            float const stepDt = (tick == 0) ? firstDt : deltaTime;
            r.ticksRan = tick + 1;
            simulator.m_orbOverlapTick = false;
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
            } else if (acted && down) {
                if (releaseAfter >= 0 && !tapReleased && tick == delay + releaseAfter) {
                    tapReleased = true;
                    shadowPlayer->releaseButton(PlayerButton::Jump);
                } else if (releaseAfter < 0 && model.tapRelease && !tapReleased &&
                           simulator.m_jumped) {
                    bool const touchingOrb =
                        simulator.m_orbOverlapTick ||
                        (shadowPlayer->m_touchingRings &&
                         shadowPlayer->m_touchingRings->count() > 0);
                    bool const dashing = shadowPlayer->m_isDashing;
                    bool const justHitOrb = simulator.m_activatedRing && tick <= delay + 4;
                    if (!touchingOrb && !dashing && !justHitOrb) {
                        tapReleased = true;
                        shadowPlayer->releaseButton(PlayerButton::Jump);
                    }
                }
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

            if (model.stopAtBallLanding && acted && shadowPlayer->m_isBall) {
                ballFlipped = ballFlipped || shadowPlayer->m_isUpsideDown != initialGravity;
                bool const grounded = shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
                if (ballFlipped && !grounded)
                    ballAirborne = true;
                ballLandingTicks = ballAirborne && grounded ? ballLandingTicks + 1 : 0;
                if (ballLandingTicks >= 2) {
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

            if (zone && zone->valid && !pastZone) {
                if (shadowPlayer->getPositionX() > zone->endX) {
                    bool settled = false;
                    if (model.continuous) {
                        settledAfterClear++;
                        settled = settledAfterClear >= 8;
                    } else {
                        bool const grounded =
                            shadowPlayer->m_isOnGround || shadowPlayer->m_isOnGround2;
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

            if (pastZone && !model.continuePastClear && tick + 1 - r.ticksBeforeNeutral > 24)
                return finish(r);
        }
        return finish(r);
    }

    static void fillPrefixDeath(EbShadowPlanContext& context, int fromDelay, int prefixDeathTick) {
        for (int delay = fromDelay; delay <= context.limit; ++delay) {
            EbSimulationCandidate skip;
            skip.delay = delay;
            skip.diedBeforeAction = true;
            skip.raw.died = true;
            skip.raw.diedBeforeAction = true;
            skip.raw.deathTick = prefixDeathTick;
            context.finalizeViable(skip, -1);
            context.cands[static_cast<size_t>(delay)] = skip;
            context.candReady[static_cast<size_t>(delay)] = 1;
        }
    }

private:
    EbIdlePrefixForkState() = delete;
};

bool EbIdlePrefixShare::evaluateAscending(EbShadowPlanContext& context) {
    if (context.useTrajEngine || !context.layer || !context.real || !context.simulator.m_shadow)
        return false;
    if (context.limit < 1)
        return true;

    PlayerObject* forkGhost = context.simulator.m_evalGhosts[0];
    if (!forkGhost)
        return false;

    auto& simulator = context.simulator;
    PlayLayer* layer = context.layer;
    bool const down = context.down;
    float const deltaTime = context.deltaTime;
    float const firstDt = context.firstDt;
    auto const& model = context.model;
    EbThreatZone const* zonePtr = context.zone.valid ? &context.zone : nullptr;

    bool const isWaveMode = context.real && context.real->m_isDart;
    bool const isShipLike = context.real && (context.real->m_isShip || context.real->m_isBird ||
                                             context.real->m_isSpider || context.real->m_isSwing);
    bool const isRobot = context.real && context.real->m_isRobot;

    int preferLastInOrder = -1;
    if (context.hasPrefer) {
        int const offset = context.robustK;
        preferLastInOrder = std::min(context.limit, context.preferHi + offset);
    }

    auto preferBandRobust = [&]() -> bool {
        if (!context.hasPrefer)
            return false;
        int const offset = context.robustK;
        for (int delay = context.preferLo; delay <= context.preferHi; ++delay) {
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            if (!candidate.viable)
                continue;
            bool ok = true;
            for (int entry = delay - offset; entry <= delay + offset; ++entry) {
                if (entry < 0 || entry > context.limit)
                    continue;
                if (!context.cands[static_cast<size_t>(entry)].viable) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return true;
        }
        for (int delay = context.preferLo; delay <= context.preferHi; ++delay) {
            if (context.cands[static_cast<size_t>(delay)].viable)
                return true;
        }
        return false;
    };

    int prefixDeathTick = context.options.seedPrefixDeathTick;
    int sameCrashStreak = 0;
    int sameCrashPrevTick = -1;
    bool sameCrashStopped = false;

    simulator.resetCaptureForNextRun(layer);
    simulator.copyPlayerState(simulator.m_shadow, context.real);
    simulator.m_dead = false;
    simulator.m_jumped = false;
    simulator.m_unknown = false;
    simulator.m_toggleUnknownHit = false;
    simulator.m_activatedRing = false;
    simulator.m_ringCount = 0;
    simulator.m_deathObject = nullptr;
    auto* shadowPlayer = simulator.m_shadow;
    shadowPlayer->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = !down;

    simulator.m_orbOverlapTick = false;
    simulator.m_simAbsTick = simulator.m_simTickBase;
    simulator.stepPreCollide(layer, shadowPlayer, deltaTime);
    if (simulator.m_dead) {
        prefixDeathTick = EbPrefixSim::mergePrefixDeathTick(prefixDeathTick, 0, true);
        EbIdlePrefixForkState::fillPrefixDeath(context, 1, 0);
        simulator.resetCaptureForNextRun(layer);
        return true;
    }
    if (simulator.m_unknown) {
        simulator.resetCaptureForNextRun(layer);
        return false;
    }

    int idleTicksDone = 0;
    auto spend = [&]() -> bool {
        if (context.tickBudget <= 0)
            return true;
        --context.tickBudget;
        return false;
    };

    for (int delay = 1; delay <= context.limit; ++delay) {
        if (context.tickBudget <= 0 || context.wallExceeded()) {
            context.plan.budgetHit = true;
            break;
        }
        if (EbPrefixSim::shouldSkipAfterPrefixDeath(delay, prefixDeathTick)) {
            EbIdlePrefixForkState::fillPrefixDeath(context, delay, prefixDeathTick);
            break;
        }

        while (idleTicksDone < delay) {
            if (spend()) {
                context.plan.budgetHit = true;
                break;
            }
            int const tick = idleTicksDone;
            float const stepDt = (tick == 0) ? firstDt : deltaTime;
            simulator.m_orbOverlapTick = false;
            simulator.m_simAbsTick = simulator.m_simTickBase + tick;
            simulator.stepPhysics(layer, shadowPlayer, stepDt, deltaTime);
            ++idleTicksDone;
            if (simulator.m_dead) {
                prefixDeathTick =
                    EbPrefixSim::mergePrefixDeathTick(prefixDeathTick, tick, true);
                EbIdlePrefixForkState::fillPrefixDeath(context, delay, prefixDeathTick);
                simulator.resetCaptureForNextRun(layer);
                return true;
            }
            if (simulator.m_unknown) {
                simulator.resetCaptureForNextRun(layer);
                return false;
            }
        }
        if (context.plan.budgetHit)
            break;

        if (context.candReady[static_cast<size_t>(delay)])
            continue;

        EbIdlePrefixForkState::ForkPoint fork;
        if (!EbIdlePrefixForkState::captureFork(simulator, layer, forkGhost, fork)) {
            simulator.resetCaptureForNextRun(layer);
            return false;
        }

        auto result = EbIdlePrefixForkState::runPressContinuation(
            simulator, layer, down, delay, context.limit, deltaTime, firstDt, model, -1, zonePtr,
            &context.tickBudget);
        ++context.plan.candidatesTried;

        if (!EbIdlePrefixForkState::restoreFork(simulator, layer, forkGhost, fork)) {
            simulator.resetCaptureForNextRun(layer);
            return false;
        }

        EbSimulationCandidate candidate;
        candidate.delay = delay;
        candidate.raw = result;
        candidate.diedBeforeAction = result.diedBeforeAction;
        candidate.activatedRing = result.activatedRing;
        candidate.unknownTrunc = result.unknownTrunc;
        candidate.toggleUnknown = result.toggleUnknown;
        if (candidate.toggleUnknown || (candidate.unknownTrunc && context.hadToggleTargets))
            context.sawToggleUnknown = true;
        candidate.endX = result.endX;
        candidate.deathX = result.deathX;
        candidate.jumpTick = result.jumped ? result.jumpTick : delay;
        candidate.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(result);
        candidate.surviveBin = EbShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
        candidate.requireHold = false;

        if (candidate.diedBeforeAction) {
            prefixDeathTick =
                EbPrefixSim::mergePrefixDeathTick(prefixDeathTick, candidate.raw.deathTick, true);
            context.finalizeViable(candidate, -1);
            context.cands[static_cast<size_t>(delay)] = candidate;
            context.candReady[static_cast<size_t>(delay)] = 1;
            if (prefixDeathTick >= 0) {
                EbIdlePrefixForkState::fillPrefixDeath(context, delay + 1, prefixDeathTick);
                break;
            }
            continue;
        }

        context.finalizeViable(candidate, candidate.releaseAfter >= 0 ? candidate.releaseAfter : -1);
        if (candidate.viable && context.linkedDual()) {
            PlayerObject* other =
                (context.real == layer->m_player1) ? layer->m_player2 : layer->m_player1;
            if (other && context.tickBudget > 0) {
                auto r2 = context.runEngine(other, delay, -1, zonePtr);
                simulator.resetCaptureForNextRun(layer);
                if (!EbShadowSurvivalRules::runResultDelaySafe(r2, context.zone, model, other,
                                                              context.requireAlive)) {
                    candidate.viable = false;
                    candidate.clearsThreat = false;
                    candidate.dualFailed = true;
                }
                context.cands[static_cast<size_t>(delay)] = candidate;
                context.candReady[static_cast<size_t>(delay)] = 1;
                // Spine destroyed by dual reset — let legacy fill remaining delays.
                EbAssistLog::debug(fmt::format(
                    "PLAN id={} idle-prefix dual reset spine after delay={}", context.plan.planId,
                    delay));
                return false;
            }
        }

        context.cands[static_cast<size_t>(delay)] = candidate;
        context.candReady[static_cast<size_t>(delay)] = 1;

        sameCrashStreak = EbPrefixSim::updateSameCrashStreak(
            sameCrashStreak, sameCrashPrevTick, candidate.raw.deathTick, candidate.raw.died,
            candidate.diedBeforeAction);
        if (candidate.raw.died && !candidate.diedBeforeAction && candidate.raw.deathTick >= 0)
            sameCrashPrevTick = candidate.raw.deathTick;
        if (EbPrefixSim::sameCrashShouldStop(sameCrashStreak)) {
            sameCrashStopped = true;
            context.ledgerSameCrashExit = true;
            EbAssistLog::debug(fmt::format(
                "PLAN id={} idle-prefix same-crash stop deathTick={} after delay={}",
                context.plan.planId, candidate.raw.deathTick, delay));
            break;
        }
        // Input-window discrete: survivor then acted death → stop (Grief cliff).
        // AP1: leave leftovers unevaluated; ledger PeakDeathCut — never fake-dead.
        bool hadSurvivor = context.candReady[0] && !context.cands[0].raw.died;
        for (int prior = 1; !hadSurvivor && prior < delay; ++prior) {
            if (context.candReady[static_cast<size_t>(prior)] &&
                !context.cands[static_cast<size_t>(prior)].raw.died)
                hadSurvivor = true;
        }
        if (EbLivePlanContracts::peakDeathShouldStop(context.inputWindow, isWaveMode || isShipLike,
                                                     hadSurvivor, candidate.raw.died &&
                                                                     !candidate.diedBeforeAction)) {
            context.ledgerPeakDeathExit = true;
            EbAssistLog::debug(fmt::format(
                "PLAN id={} idle-prefix input-window peak-death-cut after delay={}",
                context.plan.planId, delay));
            break;
        }

        bool const preferExitOk = EbMissAssistContracts::preferBandEarlyExitAllowed(
            context.orbAssist, context.orbHint.found, context.spatialHint,
            context.plan.foresightOrbPad);
        if (context.hasPrefer && preferExitOk && !context.plan.budgetHit &&
            delay == preferLastInOrder && preferBandRobust()) {
            context.ledgerPreferEarlyExit = true;
            EbAssistLog::debug(fmt::format(
                "PLAN id={} idle-prefix prefer-band early-exit [{},{}] after delay={}",
                context.plan.planId, context.preferLo, context.preferHi, delay));
            break;
        }
    }

    {
        std::vector<EbPrefixSim::QuickSample> quick;
        quick.reserve(static_cast<size_t>(context.limit) + 1);
        for (int delayValue = 0; delayValue <= context.limit; ++delayValue) {
            if (!context.candReady[static_cast<size_t>(delayValue)])
                continue;
            auto const& candidate = context.cands[static_cast<size_t>(delayValue)];
            EbPrefixSim::QuickSample sample;
            sample.delay = delayValue;
            sample.died = candidate.raw.died;
            sample.diedBeforeAction = candidate.diedBeforeAction;
            sample.viable = candidate.viable;
            sample.activatedRing = candidate.activatedRing;
            sample.deathTick = candidate.raw.deathTick;
            sample.surviveTicks = candidate.surviveTicks;
            sample.endX = candidate.endX;
            quick.push_back(sample);
        }
        auto const fullDelays = EbPrefixSim::delaysForFullScore(
            quick.data(), static_cast<int>(quick.size()), EbPrefixSim::kFullScoreTopK);
        bool const holdTheoryOk = EbPrefixSim::allowHoldBranchTheory(context.model.continuous,
                                                                   isRobot, isWaveMode, isShipLike);

        for (int delayValue : fullDelays) {
            if (delayValue < 0 || delayValue > context.limit)
                continue;
            if (context.tickBudget <= 0 || context.wallExceeded()) {
                context.plan.budgetHit = true;
                break;
            }
            auto& candidate = context.cands[static_cast<size_t>(delayValue)];

            if (candidate.raw.died && !candidate.diedBeforeAction && context.down && holdTheoryOk &&
                context.options.maxBranches > 0) {
                int tried = 0;
                for (int offset : EbShadowSurvivalRules::BRANCH_RELEASES) {
                    if (tried >= context.options.maxBranches)
                        break;
                    if (context.tickBudget <= 0)
                        break;
                    ++tried;
                    auto br = context.runEngine(context.real, delayValue, offset, zonePtr);
                    simulator.resetCaptureForNextRun(layer);
                    if (!br.died || br.unknownTrunc) {
                        candidate.raw = br;
                        candidate.activatedRing = br.activatedRing;
                        candidate.unknownTrunc = br.unknownTrunc;
                        candidate.toggleUnknown = br.toggleUnknown;
                        if (candidate.toggleUnknown ||
                            (candidate.unknownTrunc && context.hadToggleTargets)) {
                            context.sawToggleUnknown = true;
                        }
                        candidate.endX = br.endX;
                        candidate.deathX = br.deathX;
                        candidate.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(br);
                        candidate.surviveBin =
                            EbShadowSurvivalRules::surviveBinOf(candidate.surviveTicks);
                        candidate.releaseAfter = offset;
                        candidate.requireHold = false;
                        context.finalizeViable(candidate, offset);
                        break;
                    }
                }
            }

            if (candidate.raw.activatedRing && candidate.raw.wasDashing && !candidate.viable &&
                context.tickBudget > 0) {
                auto hold = context.runEngine(context.real, candidate.delay, 40, zonePtr);
                simulator.resetCaptureForNextRun(layer);
                EbSimulationCandidate height = candidate;
                height.raw = hold;
                height.activatedRing = hold.activatedRing;
                height.unknownTrunc = hold.unknownTrunc;
                height.toggleUnknown = hold.toggleUnknown;
                if (height.toggleUnknown || (height.unknownTrunc && context.hadToggleTargets))
                    context.sawToggleUnknown = true;
                height.endX = hold.endX;
                height.deathX = hold.deathX;
                height.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(hold);
                height.surviveBin = EbShadowSurvivalRules::surviveBinOf(height.surviveTicks);
                context.finalizeViable(height, 40);
                if (height.viable && !candidate.viable) {
                    candidate = height;
                    candidate.requireHold = true;
                    candidate.releaseAfter = 40;
                }
            }
        }

        if (sameCrashStopped) {
            EbAssistLog::debug(fmt::format(
                "PLAN id={} idle-prefix sameCrash=1 prefixDeathTick={}", context.plan.planId,
                prefixDeathTick));
        }
    }

    simulator.resetCaptureForNextRun(layer);
    return true;
}

bool EbIdlePrefixShare::selfCheck(std::string& report) {
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

    int const n = 8;
    int independentIdle = 0;
    for (int delay = 1; delay <= n; ++delay)
        independentIdle += delay;
    int const sharedIdle = n;
    expect(independentIdle == n * (n + 1) / 2, "indep-sum");
    expect(sharedIdle * 2 <= independentIdle, "share-leq-half");
    expect(independentIdle / sharedIdle == (n + 1) / 2, "share-factor");

    int idleTicksDone = 0;
    for (int delay = 1; delay <= 4; ++delay) {
        while (idleTicksDone < delay)
            ++idleTicksDone;
        expect(idleTicksDone == delay, "pre-press-align");
    }

    if (fails == 0) {
        report = "IdlePrefixShare selfCheck OK";
        return true;
    }
    report = fmt::format("IdlePrefixShare selfCheck {} fails: {}", fails, report);
    return false;
}
