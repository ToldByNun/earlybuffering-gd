#include "../../app/class_manager.hpp"
#include "shadow_simulator.hpp"
#include "../../dev/capture/dev_live_witness.hpp"
#include "../../dev/evaluation/dev_eval_restore.hpp"
#include "../replay/replay_import.hpp"
#include "evaluation/shadow_candidate_runner.hpp"
#include "evaluation/shadow_delay_verifier.hpp"
#include "evaluation/shadow_freeze_evaluator.hpp"
#include "planning/search/adaptive/adaptive_plan_search.hpp"
#include "planning/search/shadow_plan_search.hpp"
#include "planning/shadow_plan_policy.hpp"
#include "planning/shadow_sequence_planner.hpp"
#include "planning/simulation_plan.hpp"
#include "replay/shadow_replay_runner.hpp"
#include "runtime/shadow_ghost_lifecycle.hpp"
#include "runtime/shadow_physics.hpp"
#include "runtime/shadow_player_state.hpp"
#include "runtime/shadow_section_safety.hpp"
#include "validation/shadow_regression_checks.hpp"
#include "world/shadow_world_capture.hpp"
#include "world/shadow_world_dynamics.hpp"
#include <string>
#include <vector>

// Post-pick hold probe: how long can the player hold, and when to let go safely.

// budget: at most this many extra runOnes

EbShadowSimulator& EbShadowSimulator::get() {
    return EbClassManager::get().service<EbShadowSimulator>();
}

void EbShadowSimulator::attach(PlayLayer* layer) {
    return EbShadowGhostLifecycle::attach(*this, layer);
}

void EbShadowSimulator::detach(bool layerDying) {
    return EbShadowGhostLifecycle::detach(*this, layerDying);
}

bool EbShadowSimulator::isOwnedGhost(PlayerObject* player) const {
    return EbShadowGhostLifecycle::isOwnedGhost(*this, player);
}

void EbShadowSimulator::abortEval() {
    this->m_evalAbort = true;
}

bool EbShadowSimulator::tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object) {
    return EbShadowSectionSafety::tryRemoveFromSection(layer, object);
}

void EbShadowSimulator::scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, EbShadowSimulator* self) {
    return EbShadowSectionSafety::scrubOwnedGhostsFromSections(layer, self);
}

void EbShadowSimulator::parkGhosts() {
    return EbShadowGhostLifecycle::parkGhosts(*this);
}

void EbShadowSimulator::beginSession(PlayLayer* layer) {
    return EbShadowGhostLifecycle::beginSession(*this, layer);
}

void EbShadowSimulator::endSession(PlayLayer* layer) {
    EbShadowGhostLifecycle::endSession(*this, layer);
    clearPlanWall();
}

void EbShadowSimulator::beginPlanWall(std::chrono::steady_clock::time_point wall0, int budgetUs) {
    m_planWall0 = wall0;
    m_planWallBudgetUs = budgetUs > 0 ? budgetUs : 0;
    m_planWallHit = false;
}

void EbShadowSimulator::clearPlanWall() {
    m_planWallBudgetUs = 0;
    m_planWallHit = false;
}

bool EbShadowSimulator::planWallHit() const {
    return m_planWallHit;
}

bool EbShadowSimulator::planWallExceeded() const {
    if (m_planWallHit)
        return true;
    if (m_planWallBudgetUs <= 0)
        return false;
    auto const elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - m_planWall0)
                                         .count();
    return elapsedMicroseconds >= m_planWallBudgetUs;
}

bool EbShadowSimulator::markPlanWallIfExceeded() {
    if (!planWallExceeded())
        return false;
    m_planWallHit = true;
    return true;
}

bool EbShadowSimulator::spendPlanTick(int* tickBudgetLeft, int tickIndex) {
    if (tickBudgetLeft) {
        if (*tickBudgetLeft <= 0)
            return true;
        --(*tickBudgetLeft);
    }
    if (m_planWallHit)
        return true;
    if (m_planWallBudgetUs <= 0)
        return false;
    if (tickIndex < 0)
        return false;
    if ((tickIndex % kMidRunWallPollTicks) != 0)
        return false;
    return markPlanWallIfExceeded();
}

gd::vector<GameObject*>& EbShadowSimulator::collisionFilterScratch() {
    return this->m_collisionFilterBuf;
}

void EbShadowSimulator::evacuateGhosts(GJBaseGameLayer* layer) {
    return EbShadowSectionSafety::evacuateGhosts(*this, layer);
}

void EbShadowSimulator::markDead(PlayerObject* player, GameObject* object) {
    return EbShadowPhysics::markDead(*this, player, object);
}

void EbShadowSimulator::noteJump() {
    return EbShadowPhysics::noteJump(*this);
}

void EbShadowSimulator::hideGhost(PlayerObject* player) {
    return EbShadowGhostLifecycle::hideGhost(player);
}

void EbShadowSimulator::noteActivated(GameObject* gobj) {
    return EbShadowPhysics::noteActivated(*this, gobj);
}

void EbShadowSimulator::noteOrbOverlap() {
    return EbShadowPhysics::noteOrbOverlap(*this);
}

void EbShadowSimulator::applyRingJump(PlayerObject* shadow, RingObject* ring) {
    return EbShadowPhysics::applyRingJump(*this, shadow, ring);
}

void EbShadowSimulator::captureTriggerTransforms(PlayLayer* layer) {
    return EbShadowWorldCapture::captureTriggerTransforms(*this, layer);
}

bool EbShadowSimulator::isDynamicTriggerTarget(int uniqueId) const {
    return EbShadowWorldDynamics::isDynamicTriggerTarget(*this, uniqueId);
}

bool EbShadowSimulator::isToggleTriggerTarget(int uniqueId) const {
    return EbShadowWorldDynamics::isToggleTriggerTarget(*this, uniqueId);
}

bool EbShadowSimulator::liveDynamicApplied(int uniqueId) const {
    return EbShadowWorldDynamics::liveDynamicApplied(*this, uniqueId);
}

void EbShadowSimulator::noteUnknown(GameObject* object) {
    return EbShadowWorldDynamics::noteUnknown(*this, object);
}

void EbShadowSimulator::captureToggleUnknownTargets(PlayLayer* layer) {
    return EbShadowWorldCapture::captureToggleUnknownTargets(*this, layer);
}

void EbShadowSimulator::captureDynamicFollows(PlayLayer* layer) {
    return EbShadowWorldCapture::captureDynamicFollows(*this, layer);
}

void EbShadowSimulator::applyLiveDynamicNear(PlayLayer* layer, PlayerObject* ghost) {
    return EbShadowWorldDynamics::applyLiveDynamicNear(*this, layer, ghost);
}

void EbShadowSimulator::resetTriggerTransforms() {
    return EbShadowWorldCapture::resetTriggerTransforms(*this);
}

void EbShadowSimulator::restoreTriggerTransforms(GJBaseGameLayer* layer) {
    return EbShadowWorldCapture::restoreTriggerTransforms(*this, layer);
}

void EbShadowSimulator::beginCapture(PlayLayer* layer) {
    return EbShadowWorldCapture::beginCapture(*this, layer);
}

void EbShadowSimulator::resetCaptureForNextRun(PlayLayer* layer) {
    return EbShadowWorldCapture::resetCaptureForNextRun(*this, layer);
}

void EbShadowSimulator::restoreCapture(PlayLayer* layer) {
    return EbShadowWorldCapture::restoreCapture(*this, layer);
}

void EbShadowSimulator::copyWaveTrail(PlayerObject* destination, PlayerObject* source) {
    return EbShadowPlayerState::copyWaveTrail(*this, destination, source);
}

void EbShadowSimulator::clearCollisionLogs(PlayerObject* state) {
    return EbShadowPlayerState::clearCollisionLogs(state);
}

void EbShadowSimulator::applyTriggerWorldAtTick(PlayLayer* layer, int absTick, PlayerObject* ghost) {
    return EbShadowWorldDynamics::applyTriggerWorldAtTick(*this, layer, absTick, ghost);
}

void EbShadowSimulator::stepPreCollide(PlayLayer* layer, PlayerObject* state, float deltaTime) {
    return EbShadowPhysics::stepPreCollide(*this, layer, state, deltaTime);
}

void EbShadowSimulator::stepPhysics(PlayLayer* layer, PlayerObject* state, float stepDt,
                                    float deltaTime, bool splitPrefix,
                                    cocos2d::CCPoint const* tickStart) {
    return EbShadowPhysics::stepPhysics(*this, layer, state, stepDt, deltaTime, splitPrefix,
                                        tickStart);
}

void EbShadowSimulator::copyPlayerState(PlayerObject* destination, PlayerObject* source) {
    return EbShadowPlayerState::copyPlayerState(destination, source);
}

size_t EbShadowSimulator::diagCaptureTransformCount(PlayLayer* layer) {
    return EbShadowPlayerState::diagCaptureTransformCount(*this, layer);
}

int EbShadowSimulator::captureStartSnapshot(PlayerObject* real) {
    return EbShadowPlayerState::captureStartSnapshot(*this, real);
}

int EbShadowSimulator::captureSnapshot(PlayerObject* real) {
    return EbShadowPlayerState::captureSnapshot(*this, real);
}

int EbShadowSimulator::captureParitySnapshot(PlayerObject* real, int which) {
    return EbShadowPlayerState::captureParitySnapshot(*this, real, which);
}

PlayerObject* EbShadowSimulator::snapshotSlot(int slot) const {
    return EbShadowPlayerState::snapshotSlot(*this, slot);
}

EbShadowSimulator::Model EbShadowSimulator::modelFor(PlayerObject* player, int limit, float capMul) {
    return EbShadowPlanPolicy::modelFor(player, limit, capMul);
}

bool EbShadowSimulator::isHazardLike(GameObject* object) {
    return EbShadowPlanPolicy::isHazardLike(object);
}

EbThreatZone EbShadowSimulator::buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj) {
    return EbShadowPlanPolicy::buildThreatZone(layer, deathX, deathObj);
}

bool EbShadowSimulator::spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit,
                                     float deltaTime) {
    return EbShadowPlanPolicy::spatialOrbHint(layer, real, limit, deltaTime);
}

bool EbShadowSimulator::spatialHazardProximity(PlayLayer* layer, PlayerObject* real,
                                             float proximityX) {
    return EbShadowPlanPolicy::spatialHazardProximity(layer, real, proximityX);
}

bool EbShadowSimulator::shouldWaveHazardProximityPass(bool isWave, bool nearHazard, bool noOrbTarget,
                                                    bool foresightOrbPad, bool pressDown) {
    return EbShadowPlanPolicy::shouldWaveHazardProximityPass(isWave, nearHazard, noOrbTarget,
                                                           foresightOrbPad, pressDown);
}

bool EbShadowSimulator::shouldWaveFutileDelayPass(bool isWave, bool orbIntent, int bestSurviveBin,
                                                float bestEndXGain, bool foresightOrbPad,
                                                bool pressDown) {
    return EbShadowPlanPolicy::shouldWaveFutileDelayPass(isWave, orbIntent, bestSurviveBin,
                                                       bestEndXGain, foresightOrbPad, pressDown);
}

bool EbShadowSimulator::deathLooksLikeAscent(float playerY, float deathY, bool upsideDown,
                                           float marginY) {
    return EbShadowPlanPolicy::deathLooksLikeAscent(playerY, deathY, upsideDown, marginY);
}

bool EbShadowSimulator::classifyIdleSafer(bool continuous, bool pressDown, bool baselineDied,
                                        int baselineDeathTick, float baselineDeathX, bool d0Died,
                                        int d0DeathTick, float d0DeathX) {
    return EbShadowPlanPolicy::classifyIdleSafer(continuous, pressDown, baselineDied,
                                               baselineDeathTick, baselineDeathX, d0Died,
                                               d0DeathTick, d0DeathX);
}

bool EbShadowSimulator::classifyAscentUseful(bool continuous, bool pressDown, bool d0Viable,
                                           int d0SurviveBin, float d0EndX, bool laterViable,
                                           int laterSurviveBin, float laterEndX, bool laterAlive) {
    return EbShadowPlanPolicy::classifyAscentUseful(continuous, pressDown, d0Viable, d0SurviveBin,
                                                  d0EndX, laterViable, laterSurviveBin, laterEndX,
                                                  laterAlive);
}

int EbShadowSimulator::pickPreferredRelease(int earliest, int latest, bool continuous) {
    return EbShadowPlanPolicy::pickPreferredRelease(earliest, latest, continuous);
}

int EbShadowSimulator::autoRobustKHints(bool waveLike, bool isWave, float playerSpeed,
                                      float dtVariance, bool spamDense) {
    return EbShadowPlanPolicy::autoRobustKHints(waveLike, isWave, playerSpeed, dtVariance, spamDense);
}

int EbShadowSimulator::autoRobustK(PlayerObject* real, float dtVariance, bool spamDense) {
    return EbShadowPlanPolicy::autoRobustK(real, dtVariance, spamDense);
}

EbShadowSimulator::RunResult
EbShadowSimulator::runOne(PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
                          float deltaTime, float firstDt, Model const& model, int releaseAfter,
                          EbThreatZone const* zone, int* tickBudgetLeft, float firePhase) {
    return EbShadowCandidateRunner::runOne(*this, layer, real, down, delay, limit, deltaTime,
                                           firstDt, model, releaseAfter, zone, tickBudgetLeft,
                                           firePhase);
}

EbShadowSimulator::RunResult
EbShadowSimulator::runTrajectoryOne(PlayLayer* layer, PlayerObject* real, bool down, int delay,
                                  int limit, float deltaTime, float firstDt, Model const& model,
                                  int releaseAfter, EbThreatZone const* zone, int* tickBudgetLeft) {
    return EbShadowCandidateRunner::runTrajectoryOne(*this, layer, real, down, delay, limit,
                                                   deltaTime, firstDt, model, releaseAfter, zone,
                                                   tickBudgetLeft);
}

EbShadowSimulator::RunResult EbShadowSimulator::runBaseline(PlayLayer* layer, PlayerObject* real,
                                                        bool down, float deltaTime, float firstDt,
                                                        Model const& model, int* tickBudgetLeft) {
    return EbShadowCandidateRunner::runBaseline(*this, layer, real, down, deltaTime, firstDt, model,
                                              tickBudgetLeft);
}

EbShadowSimulator::OrbWindow EbShadowSimulator::scanOrbWindow(PlayLayer* layer, PlayerObject* real,
                                                          bool down, int scanTicks, float deltaTime,
                                                          float firstDt, int* tickBudgetLeft,
                                                          bool holdJump) {
    return EbShadowCandidateRunner::scanOrbWindow(*this, layer, real, down, scanTicks, deltaTime,
                                                firstDt, tickBudgetLeft, holdJump);
}

// Score honesty + ModeSurvival:
// Delay-viable needs clearsThreat (or no zone / zoneCleared). surviveBin alone
// is NOT enough. Unknown never viable.
// Soft (requireAliveForDelay=false): died may still be not-false-lethal when
// Landing / Recoverable / LateCrashAfterClear (bake verify).
// Live arm (requireAliveForDelay=true): Delay-worthy = Alive only; Landing /

EbShadowSimulator::VerifyDelayResult
EbShadowSimulator::verifyDelay(PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
                             float deltaTime, float firstDt, EbPlanOptions const& options) {
    return EbShadowDelayVerifier::verifyDelay(*this, layer, real, down, delay, limit, deltaTime,
                                            firstDt, options);
}

EbSimPlan EbShadowSimulator::plan(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                                float deltaTime, float firstDt, EbPlanOptions const& options) {
    return EbAdaptivePlanSearch::run(*this, layer, real, down, limit, deltaTime, firstDt, options);
}

EbSimPlan EbShadowSimulator::planSequence(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                                      float deltaTime, float firstDt, EbPlanOptions const& options) {
    return EbShadowSequencePlanner::planSequence(*this, layer, real, down, limit, deltaTime, firstDt,
                                               options);
}

EbShadowSimulator::ReplayResult EbShadowSimulator::replay(PlayLayer* layer, int slot, int startTick,
                                                      std::vector<EbInputEvent> const& timeline,
                                                      float deltaTime, int maxTicks,
                                                      bool shiftActionBy, int actionShift) {
    return EbShadowReplayRunner::replay(*this, layer, slot, startTick, timeline, deltaTime, maxTicks,
                                      shiftActionBy, actionShift);
}

EbShadowSimulator::BakeRunResult
EbShadowSimulator::runBakeTimeline(PlayLayer* layer, int slot, std::vector<EbReplayEvent> const& events,
                                 float deltaTime, int untilFrame, int sampleFrame,
                                 bool applyP2ToShadow, bool traceFrames, bool parkReals,
                                 int substeps, std::function<bool(int frame)> onFrame) {
    return EbShadowReplayRunner::runBakeTimeline(*this, layer, slot, events, deltaTime, untilFrame,
                                               sampleFrame, applyP2ToShadow, traceFrames, parkReals,
                                               substeps, onFrame);
}

EbShadowSimulator::PathSample EbShadowSimulator::samplePath(PlayLayer* layer, int slot,
                                                        std::vector<EbReplayEvent> const& events,
                                                        float deltaTime, int untilFrame) {
    return EbShadowReplayRunner::samplePath(*this, layer, slot, events, deltaTime, untilFrame);
}

bool EbShadowSimulator::regressionSelfCheck(std::string& report) {
    return EbShadowRegressionChecks::regressionSelfCheck(report);
}

void EbShadowSimulator::applyPlayerBlob(PlayerObject* destination, EbDevPlayerBlob const& blob) {
    return EbShadowFreezeEvaluator::applyPlayerBlob(destination, blob);
}

void EbShadowSimulator::applyEvalWorldRestore(PlayLayer* layer, EbDevLiveWitness const& witness,
                                            EbDevEvalRestore& restore) {
    return EbShadowFreezeEvaluator::applyEvalWorldRestore(*this, layer, witness, restore);
}

void EbShadowSimulator::finishEvalWorldRestore(PlayLayer* layer) {
    return EbShadowFreezeEvaluator::finishEvalWorldRestore(*this, layer);
}

EbShadowSimulator::EvalFromFreezeResult
EbShadowSimulator::evalDelayFromFreeze(PlayLayer* layer, EbDevLiveWitness const& witness, int delay,
                                       int horizonTicks, bool down, int* tickBudgetLeft,
                                       bool captureTraj, float firePhase) {
    return EbShadowFreezeEvaluator::evalDelayFromFreeze(
        *this, layer, witness, delay, horizonTicks, down, tickBudgetLeft, captureTraj, firePhase);
}

EbShadowSimulator::EvalFromFreezeResult
EbShadowSimulator::replayActualFire(PlayLayer* layer, EbDevLiveWitness const& witness,
                                  int actualFireTickAbs, int horizonTicks, bool down,
                                  int* tickBudgetLeft) {
    return EbShadowFreezeEvaluator::replayActualFire(*this, layer, witness, actualFireTickAbs,
                                                   horizonTicks, down, tickBudgetLeft);
}
