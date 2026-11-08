#include "shadow_simulator.hpp"
#include "../../dev/capture/dev_live_witness.hpp"
#include "../../dev/evaluation/dev_eval_restore.hpp"
#include "../replay/replay_import.hpp"
#include "evaluation/shadow_candidate_runner.hpp"
#include "evaluation/shadow_delay_verifier.hpp"
#include "evaluation/shadow_freeze_evaluator.hpp"
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

ShadowSimulator& ShadowSimulator::get() {
    static ShadowSimulator inst;
    return inst;
}

void ShadowSimulator::attach(PlayLayer* layer) {
    return ShadowGhostLifecycle::attach(*this, layer);
}

void ShadowSimulator::detach(bool layerDying) {
    return ShadowGhostLifecycle::detach(*this, layerDying);
}

bool ShadowSimulator::isOwnedGhost(PlayerObject* player) const {
    return ShadowGhostLifecycle::isOwnedGhost(*this, player);
}

void ShadowSimulator::abortEval() {
    this->m_evalAbort = true;
}

bool ShadowSimulator::tryRemoveFromSection(GJBaseGameLayer* layer, GameObject* object) {
    return ShadowSectionSafety::tryRemoveFromSection(layer, object);
}

void ShadowSimulator::scrubOwnedGhostsFromSections(GJBaseGameLayer* layer, ShadowSimulator* self) {
    return ShadowSectionSafety::scrubOwnedGhostsFromSections(layer, self);
}

void ShadowSimulator::parkGhosts() {
    return ShadowGhostLifecycle::parkGhosts(*this);
}

void ShadowSimulator::beginSession(PlayLayer* layer) {
    return ShadowGhostLifecycle::beginSession(*this, layer);
}

void ShadowSimulator::endSession(PlayLayer* layer) {
    return ShadowGhostLifecycle::endSession(*this, layer);
}

gd::vector<GameObject*>& ShadowSimulator::collisionFilterScratch() {
    return this->m_collisionFilterBuf;
}

void ShadowSimulator::evacuateGhosts(GJBaseGameLayer* layer) {
    return ShadowSectionSafety::evacuateGhosts(*this, layer);
}

void ShadowSimulator::markDead(PlayerObject* player, GameObject* object) {
    return ShadowPhysics::markDead(*this, player, object);
}

void ShadowSimulator::noteJump() {
    return ShadowPhysics::noteJump(*this);
}

void ShadowSimulator::hideGhost(PlayerObject* player) {
    return ShadowGhostLifecycle::hideGhost(player);
}

void ShadowSimulator::noteActivated(GameObject* gobj) {
    return ShadowPhysics::noteActivated(*this, gobj);
}

void ShadowSimulator::noteOrbOverlap() {
    return ShadowPhysics::noteOrbOverlap(*this);
}

void ShadowSimulator::applyRingJump(PlayerObject* shadow, RingObject* ring) {
    return ShadowPhysics::applyRingJump(*this, shadow, ring);
}

void ShadowSimulator::captureTriggerTransforms(PlayLayer* layer) {
    return ShadowWorldCapture::captureTriggerTransforms(*this, layer);
}

bool ShadowSimulator::isDynamicTriggerTarget(int uniqueId) const {
    return ShadowWorldDynamics::isDynamicTriggerTarget(*this, uniqueId);
}

bool ShadowSimulator::isToggleTriggerTarget(int uniqueId) const {
    return ShadowWorldDynamics::isToggleTriggerTarget(*this, uniqueId);
}

bool ShadowSimulator::liveDynamicApplied(int uniqueId) const {
    return ShadowWorldDynamics::liveDynamicApplied(*this, uniqueId);
}

void ShadowSimulator::noteUnknown(GameObject* object) {
    return ShadowWorldDynamics::noteUnknown(*this, object);
}

void ShadowSimulator::captureToggleUnknownTargets(PlayLayer* layer) {
    return ShadowWorldCapture::captureToggleUnknownTargets(*this, layer);
}

void ShadowSimulator::captureDynamicFollows(PlayLayer* layer) {
    return ShadowWorldCapture::captureDynamicFollows(*this, layer);
}

void ShadowSimulator::applyLiveDynamicNear(PlayLayer* layer, PlayerObject* ghost) {
    return ShadowWorldDynamics::applyLiveDynamicNear(*this, layer, ghost);
}

void ShadowSimulator::resetTriggerTransforms() {
    return ShadowWorldCapture::resetTriggerTransforms(*this);
}

void ShadowSimulator::restoreTriggerTransforms(GJBaseGameLayer* layer) {
    return ShadowWorldCapture::restoreTriggerTransforms(*this, layer);
}

void ShadowSimulator::beginCapture(PlayLayer* layer) {
    return ShadowWorldCapture::beginCapture(*this, layer);
}

void ShadowSimulator::resetCaptureForNextRun(PlayLayer* layer) {
    return ShadowWorldCapture::resetCaptureForNextRun(*this, layer);
}

void ShadowSimulator::restoreCapture(PlayLayer* layer) {
    return ShadowWorldCapture::restoreCapture(*this, layer);
}

void ShadowSimulator::copyWaveTrail(PlayerObject* destination, PlayerObject* source) {
    return ShadowPlayerState::copyWaveTrail(*this, destination, source);
}

void ShadowSimulator::clearCollisionLogs(PlayerObject* state) {
    return ShadowPlayerState::clearCollisionLogs(state);
}

void ShadowSimulator::applyTriggerWorldAtTick(PlayLayer* layer, int absTick, PlayerObject* ghost) {
    return ShadowWorldDynamics::applyTriggerWorldAtTick(*this, layer, absTick, ghost);
}

void ShadowSimulator::stepPreCollide(PlayLayer* layer, PlayerObject* state, float deltaTime) {
    return ShadowPhysics::stepPreCollide(*this, layer, state, deltaTime);
}

void ShadowSimulator::stepPhysics(PlayLayer* layer, PlayerObject* state, float stepDt,
                                  float deltaTime) {
    return ShadowPhysics::stepPhysics(*this, layer, state, stepDt, deltaTime);
}

void ShadowSimulator::copyPlayerState(PlayerObject* destination, PlayerObject* source) {
    return ShadowPlayerState::copyPlayerState(destination, source);
}

size_t ShadowSimulator::diagCaptureTransformCount(PlayLayer* layer) {
    return ShadowPlayerState::diagCaptureTransformCount(*this, layer);
}

int ShadowSimulator::captureStartSnapshot(PlayerObject* real) {
    return ShadowPlayerState::captureStartSnapshot(*this, real);
}

int ShadowSimulator::captureSnapshot(PlayerObject* real) {
    return ShadowPlayerState::captureSnapshot(*this, real);
}

int ShadowSimulator::captureParitySnapshot(PlayerObject* real, int which) {
    return ShadowPlayerState::captureParitySnapshot(*this, real, which);
}

PlayerObject* ShadowSimulator::snapshotSlot(int slot) const {
    return ShadowPlayerState::snapshotSlot(*this, slot);
}

ShadowSimulator::Model ShadowSimulator::modelFor(PlayerObject* player, int limit, float capMul) {
    return ShadowPlanPolicy::modelFor(player, limit, capMul);
}

bool ShadowSimulator::isHazardLike(GameObject* object) {
    return ShadowPlanPolicy::isHazardLike(object);
}

ThreatZone ShadowSimulator::buildThreatZone(PlayLayer* layer, float deathX, GameObject* deathObj) {
    return ShadowPlanPolicy::buildThreatZone(layer, deathX, deathObj);
}

bool ShadowSimulator::spatialOrbHint(PlayLayer* layer, PlayerObject* real, int limit,
                                     float deltaTime) {
    return ShadowPlanPolicy::spatialOrbHint(layer, real, limit, deltaTime);
}

bool ShadowSimulator::spatialHazardProximity(PlayLayer* layer, PlayerObject* real,
                                             float proximityX) {
    return ShadowPlanPolicy::spatialHazardProximity(layer, real, proximityX);
}

bool ShadowSimulator::shouldWaveHazardProximityPass(bool isWave, bool nearHazard, bool noOrbTarget,
                                                    bool foresightOrbPad, bool pressDown) {
    return ShadowPlanPolicy::shouldWaveHazardProximityPass(isWave, nearHazard, noOrbTarget,
                                                           foresightOrbPad, pressDown);
}

bool ShadowSimulator::shouldWaveFutileDelayPass(bool isWave, bool orbIntent, int bestSurviveBin,
                                                float bestEndXGain, bool foresightOrbPad,
                                                bool pressDown) {
    return ShadowPlanPolicy::shouldWaveFutileDelayPass(isWave, orbIntent, bestSurviveBin,
                                                       bestEndXGain, foresightOrbPad, pressDown);
}

bool ShadowSimulator::deathLooksLikeAscent(float playerY, float deathY, bool upsideDown,
                                           float marginY) {
    return ShadowPlanPolicy::deathLooksLikeAscent(playerY, deathY, upsideDown, marginY);
}

bool ShadowSimulator::classifyIdleSafer(bool continuous, bool pressDown, bool baselineDied,
                                        int baselineDeathTick, float baselineDeathX, bool d0Died,
                                        int d0DeathTick, float d0DeathX) {
    return ShadowPlanPolicy::classifyIdleSafer(continuous, pressDown, baselineDied,
                                               baselineDeathTick, baselineDeathX, d0Died,
                                               d0DeathTick, d0DeathX);
}

bool ShadowSimulator::classifyAscentUseful(bool continuous, bool pressDown, bool d0Viable,
                                           int d0SurviveBin, float d0EndX, bool laterViable,
                                           int laterSurviveBin, float laterEndX, bool laterAlive) {
    return ShadowPlanPolicy::classifyAscentUseful(continuous, pressDown, d0Viable, d0SurviveBin,
                                                  d0EndX, laterViable, laterSurviveBin, laterEndX,
                                                  laterAlive);
}

int ShadowSimulator::pickPreferredRelease(int earliest, int latest, bool continuous) {
    return ShadowPlanPolicy::pickPreferredRelease(earliest, latest, continuous);
}

int ShadowSimulator::autoRobustKHints(bool waveLike, bool isWave, float playerSpeed,
                                      float dtVariance, bool spamDense) {
    return ShadowPlanPolicy::autoRobustKHints(waveLike, isWave, playerSpeed, dtVariance, spamDense);
}

int ShadowSimulator::autoRobustK(PlayerObject* real, float dtVariance, bool spamDense) {
    return ShadowPlanPolicy::autoRobustK(real, dtVariance, spamDense);
}

ShadowSimulator::RunResult ShadowSimulator::runOne(PlayLayer* layer, PlayerObject* real, bool down,
                                                   int delay, int limit, float deltaTime,
                                                   float firstDt, Model const& model,
                                                   int releaseAfter, ThreatZone const* zone,
                                                   int* tickBudgetLeft) {
    return ShadowCandidateRunner::runOne(*this, layer, real, down, delay, limit, deltaTime, firstDt,
                                         model, releaseAfter, zone, tickBudgetLeft);
}

ShadowSimulator::RunResult
ShadowSimulator::runTrajectoryOne(PlayLayer* layer, PlayerObject* real, bool down, int delay,
                                  int limit, float deltaTime, float firstDt, Model const& model,
                                  int releaseAfter, ThreatZone const* zone, int* tickBudgetLeft) {
    return ShadowCandidateRunner::runTrajectoryOne(*this, layer, real, down, delay, limit,
                                                   deltaTime, firstDt, model, releaseAfter, zone,
                                                   tickBudgetLeft);
}

ShadowSimulator::RunResult ShadowSimulator::runBaseline(PlayLayer* layer, PlayerObject* real,
                                                        bool down, float deltaTime, float firstDt,
                                                        Model const& model, int* tickBudgetLeft) {
    return ShadowCandidateRunner::runBaseline(*this, layer, real, down, deltaTime, firstDt, model,
                                              tickBudgetLeft);
}

ShadowSimulator::OrbWindow ShadowSimulator::scanOrbWindow(PlayLayer* layer, PlayerObject* real,
                                                          bool down, int scanTicks, float deltaTime,
                                                          float firstDt, int* tickBudgetLeft,
                                                          bool holdJump) {
    return ShadowCandidateRunner::scanOrbWindow(*this, layer, real, down, scanTicks, deltaTime,
                                                firstDt, tickBudgetLeft, holdJump);
}

// Score honesty + ModeSurvival:
// Delay-viable needs clearsThreat (or no zone / zoneCleared). surviveBin alone
// is NOT enough. Unknown never viable.
// Soft (requireAliveForDelay=false): died may still be not-false-lethal when
// Landing / Recoverable / LateCrashAfterClear (bake verify).
// Live arm (requireAliveForDelay=true): Delay-worthy = Alive only; Landing /

ShadowSimulator::VerifyDelayResult
ShadowSimulator::verifyDelay(PlayLayer* layer, PlayerObject* real, bool down, int delay, int limit,
                             float deltaTime, float firstDt, PlanOptions const& options) {
    return ShadowDelayVerifier::verifyDelay(*this, layer, real, down, delay, limit, deltaTime,
                                            firstDt, options);
}

SimPlan ShadowSimulator::plan(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                              float deltaTime, float firstDt, PlanOptions const& options) {
    return ShadowPlanSearch::run(*this, layer, real, down, limit, deltaTime, firstDt, options);
}

SimPlan ShadowSimulator::planSequence(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                                      float deltaTime, float firstDt, PlanOptions const& options) {
    return ShadowSequencePlanner::planSequence(*this, layer, real, down, limit, deltaTime, firstDt,
                                               options);
}

ShadowSimulator::ReplayResult ShadowSimulator::replay(PlayLayer* layer, int slot, int startTick,
                                                      std::vector<InputEvent> const& timeline,
                                                      float deltaTime, int maxTicks,
                                                      bool shiftActionBy, int actionShift) {
    return ShadowReplayRunner::replay(*this, layer, slot, startTick, timeline, deltaTime, maxTicks,
                                      shiftActionBy, actionShift);
}

ShadowSimulator::BakeRunResult
ShadowSimulator::runBakeTimeline(PlayLayer* layer, int slot, std::vector<ReplayEvent> const& events,
                                 float deltaTime, int untilFrame, int sampleFrame,
                                 bool applyP2ToShadow, bool traceFrames, bool parkReals,
                                 int substeps, std::function<bool(int frame)> onFrame) {
    return ShadowReplayRunner::runBakeTimeline(*this, layer, slot, events, deltaTime, untilFrame,
                                               sampleFrame, applyP2ToShadow, traceFrames, parkReals,
                                               substeps, onFrame);
}

ShadowSimulator::PathSample ShadowSimulator::samplePath(PlayLayer* layer, int slot,
                                                        std::vector<ReplayEvent> const& events,
                                                        float deltaTime, int untilFrame) {
    return ShadowReplayRunner::samplePath(*this, layer, slot, events, deltaTime, untilFrame);
}

bool ShadowSimulator::regressionSelfCheck(std::string& report) {
    return ShadowRegressionChecks::regressionSelfCheck(report);
}

void ShadowSimulator::applyPlayerBlob(PlayerObject* destination, DevPlayerBlob const& blob) {
    return ShadowFreezeEvaluator::applyPlayerBlob(destination, blob);
}

void ShadowSimulator::applyEvalWorldRestore(PlayLayer* layer, DevLiveWitness const& witness,
                                            DevEvalRestore& restore) {
    return ShadowFreezeEvaluator::applyEvalWorldRestore(*this, layer, witness, restore);
}

void ShadowSimulator::finishEvalWorldRestore(PlayLayer* layer) {
    return ShadowFreezeEvaluator::finishEvalWorldRestore(*this, layer);
}

ShadowSimulator::EvalFromFreezeResult
ShadowSimulator::evalDelayFromFreeze(PlayLayer* layer, DevLiveWitness const& witness, int delay,
                                     int horizonTicks, bool down, int* tickBudgetLeft) {
    return ShadowFreezeEvaluator::evalDelayFromFreeze(*this, layer, witness, delay, horizonTicks,
                                                      down, tickBudgetLeft);
}

ShadowSimulator::EvalFromFreezeResult
ShadowSimulator::replayActualFire(PlayLayer* layer, DevLiveWitness const& witness,
                                  int actualFireTickAbs, int horizonTicks, bool down,
                                  int* tickBudgetLeft) {
    return ShadowFreezeEvaluator::replayActualFire(*this, layer, witness, actualFireTickAbs,
                                                   horizonTicks, down, tickBudgetLeft);
}
