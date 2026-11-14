#include "../../app/class_manager.hpp"
#include "assist_controller.hpp"
#include "../../config/stage_config.hpp"
#include "../../dev/capture/dev_clock_contract.hpp"
#include "../bake/bake_map.hpp"
#include "../bake/bake_verifier.hpp"
#include "../diagnostics/decision_log.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/shadow_simulator.hpp"
#include "assist_clock.hpp"
#include "contracts/assist_timing_rules.hpp"
#include "contracts/assist_verdict.hpp"
#include "input/assist_input_gate.hpp"
#include "input/click_intent_guard.hpp"
#include "input/hold_until_safe_policy.hpp"
#include "planning/live/assist_bake_reuse.hpp"
#include "planning/live/assist_press_planner.hpp"
#include "planning/live/assist_release_planner.hpp"
#include "reporting/assist_outcomes.hpp"
#include "reporting/assist_overlay.hpp"
#include "runtime/assist_buffer_runtime.hpp"
#include "runtime/assist_hold_runtime.hpp"
#include "runtime/assist_input_runtime.hpp"
#include "runtime/assist_lifecycle.hpp"
#include <algorithm>
#include <cmath>
#include <string>

int EbAssistControllerImplementation::roundBias(float bias) {
    if (!(bias >= 0.f)) {
        return static_cast<int>(std::lround(bias));
    }
    return static_cast<int>(std::lround(bias));
}

void EbAssistControllerImplementation::fillPlanRobustLatency(EbPlanOptions& opts, PlayerObject* player,
                                                           float dtVariance, bool spamDense) {
    opts.robustK = EbStageConfig::robustK();
    opts.robustJitter = EbStageConfig::robustJitter();
    opts.dtVariance = dtVariance;
    opts.spamDense = spamDense;
    if (player) {
        opts.latencyWeight = EbStageConfig::paramsFor(player).latencyWeight;
        // Wave λ=1.0 prefers earliest hard — in spam+orb assists that phase-shifts
        // into spikes. Soften only on spam-dense assists (Pass-excluded paths).
        if (spamDense && player->m_isDart && opts.latencyWeight > 0.5f) {
            opts.latencyWeight *= 0.65f;
        }
    }
}

// Map a nearby BakeMap window onto delay ticks for Live prefer-band ordering.
// Absolute bake frames → live ticks (unmapTick), then delays relative to effectTick.
void EbAssistControllerImplementation::fillBakePreferBandFromReuse(EbPlanOptions& opts,
                                                                 EbHybridPressReuse const& reuse,
                                                                 int effectTick, int limit,
                                                                 float simDt) {
    if (!reuse.hasWindow || limit <= 0 || simDt <= 0.f)
        return;
    double const tps = reuse.bakeTps > 0.0 ? reuse.bakeTps : EbAssistClock::kNominalTps;
    auto& clock = EbAssistClock::get();
    int eAbs = clock.unmapTick(tps, reuse.earliest, simDt);
    int lAbs = clock.unmapTick(tps, reuse.latest, simDt);
    int nAbs = clock.unmapTick(tps, reuse.nominalFrame, simDt);
    if (lAbs < eAbs)
        std::swap(lAbs, eAbs);
    // Prefer band toward nominal when still before it.
    int entry = std::max(0, eAbs - effectTick);
    int l = std::max(0, lAbs - effectTick);
    int count = std::max(0, nAbs - effectTick);
    if (count >= entry && count <= l) {
        // Tighten prefer toward nominal..latest when inside the window ahead.
        entry = count;
    }
    entry = std::max(0, std::min(limit, entry));
    l = std::max(0, std::min(limit, l));
    if (entry > l)
        return;
    opts.preferDelayMin = entry;
    opts.preferDelayMax = l;
}

void EbAssistControllerImplementation::fillBakePreferBand(EbPlanOptions& opts, PlayerObject* player,
                                                        bool player1, bool down, int effectTick,
                                                        int limit, int attemptTick, float simDt,
                                                        EbHybridPressReuse const* reuse) {
    if (reuse && reuse->hasWindow) {
        EbAssistControllerImplementation::fillBakePreferBandFromReuse(opts, *reuse, effectTick, limit,
                                                                    simDt);
        return;
    }
    if (!player || limit <= 0 || simDt <= 0.f)
        return;
    auto const& bake = EbBakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty())
        return;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : EbAssistClock::kNominalTps;
    int const approxFrame = EbAssistClock::get().remapTick(tps, attemptTick, simDt);
    auto const* win = bake.findNear(player->getPositionX(), approxFrame, down, !player1,
                                    EbStageConfig::kindOf(player));
    if (!win)
        return;

    EbHybridPressReuse tmp;
    tmp.hasWindow = true;
    tmp.earliest = win->earliest;
    tmp.latest = win->latest;
    tmp.nominalFrame = win->nominalFrame;
    tmp.bakeTps = tps;
    EbAssistControllerImplementation::fillBakePreferBandFromReuse(opts, tmp, effectTick, limit,
                                                                simDt);
}

// Nearby bake click → ClickIntentGuard must not Pass (assist allowed).
// Hybrid callers clear this after bake verify rejects the window (avoid forcing
// full Live on every moderate-CPS click near a stale bake).
bool EbAssistControllerImplementation::nearBakeWindowHint(PlayerObject* player, bool player1,
                                                        bool down, int attemptTick, float simDt,
                                                        EbHybridPressReuse const* reuse) {
    if (reuse)
        return reuse->hasWindow;
    if (!player || simDt <= 0.f)
        return false;
    auto const& bake = EbBakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty())
        return false;
    double tps = bake.data().tps > 0.0 ? bake.data().tps : EbAssistClock::kNominalTps;
    int const approxFrame = EbAssistClock::get().remapTick(tps, attemptTick, simDt);
    return bake.findNear(player->getPositionX(), approxFrame, down, !player1,
                         EbStageConfig::kindOf(player)) != nullptr;
}

// Hybrid Live fallthrough: never HUS-inject before bake nominal (press or release).
// Returns true when caller should Pass instead of armHoldUntilSafe.
bool EbAssistControllerImplementation::husBlockedBeforeBakeNominal(EbHybridPressReuse const& reuse,
                                                                 int effectTick, float simDt,
                                                                 int& outNomTick) {
    outNomTick = -1;
    if (!reuse.hasWindow || simDt <= 0.f)
        return false;
    double const bakeTps = reuse.bakeTps > 0.0 ? reuse.bakeTps : EbAssistClock::kNominalTps;
    outNomTick = EbAssistClock::get().unmapTick(bakeTps, reuse.nominalFrame, simDt);
    return effectTick < outNomTick;
}

bool EbAssistControllerImplementation::nearOrbOrPadHint(PlayLayer* playLayer, PlayerObject* player,
                                                      int limit, float simDt) {
    if (!EbStageConfig::assistOrbs())
        return false;
    if (!(!playLayer || !player || limit <= 0 || simDt <= 0.f)) {
        return EbShadowSimulator::spatialOrbHint(playLayer, player, limit, simDt);
    }
    return false;
}

EbAssistController& EbAssistController::get() {
    return EbClassManager::get().service<EbAssistController>();
}

bool EbAssistController::isInjecting() const {
    return m_injecting;
}
EbBufferedInput const& EbAssistController::buffer() const {
    return m_buffer;
}
int EbAssistController::attemptTick() const {
    return m_attemptTick;
}
float EbAssistController::lastDt() const {
    return m_lastDt;
}

double EbAssistController::estimatedTps() const {
    return EbAssistClock::get().simTpsFromDt(this->m_simDt);
}

double EbAssistController::wallTps() const {
    return EbAssistClock::get().wallTpsFromDt(this->m_lastDt);
}

std::string EbAssistController::overlayText() const {
    return m_overlayText;
}

void EbAssistController::clearBuffer() {
    m_buffer = {};
}

void EbAssistController::onCheckpointLoad() {
    EbClassManager::get().onCheckpointLoad(PlayLayer::get());
}

void EbAssistController::onLevelComplete(PlayLayer* playLayer) {
    EbClassManager::get().onLevelComplete(playLayer);
}

void EbAssistController::syncButtonState(PlayLayer* playLayer) {
    return EbAssistLifecycle::syncButtonState(*this, playLayer);
}

void EbAssistController::resetAttempt(PlayLayer* playLayer) {
    EbClassManager::get().resetAttempt(playLayer);
}

void EbAssistController::onLevelInit(PlayLayer* playLayer) {
    EbClassManager::get().onLevelInit(playLayer);
}

void EbAssistController::onLevelQuit() {
    EbClassManager::get().onLevelQuit(PlayLayer::get());
}

geode::Result<> EbAssistController::ensureStartSnapshot(PlayLayer* layer) {
    return EbAssistLifecycle::ensureStartSnapshot(*this, layer);
}

geode::Result<int> EbAssistController::runBakeMap(PlayLayer* layer, bool bruteForce) {
    return EbAssistLifecycle::runBakeMap(*this, layer, bruteForce);
}

void EbAssistController::resetLevelProfile() {
    return EbAssistLifecycle::resetLevelProfile(*this);
}

void EbAssistController::beginRealUpdate() {
    return EbAssistLifecycle::beginRealUpdate(*this);
}

float EbAssistController::bufferedSubstepPrefix(PlayerObject* player, float deltaTime) {
    return EbAssistBufferRuntime::substepPrefix(*this, player, deltaTime);
}

void EbAssistController::fireBufferedSubstep(PlayerObject* player) {
    EbAssistBufferRuntime::fireSubstep(*this, player);
}

void EbAssistController::endRealUpdate() {
    return EbAssistLifecycle::endRealUpdate(*this);
}

void EbAssistController::onSubStep(PlayerObject* player, float deltaTime) {
    return EbAssistLifecycle::onSubStep(*this, player, deltaTime);
}

bool EbAssistController::continuousModeKind(GameModeKind kind) {
    return EbAssistInputGate::continuousModeKind(kind);
}

bool EbAssistController::continuousModePlayer(PlayerObject* player) {
    return EbAssistInputGate::continuousModePlayer(player);
}

bool EbAssistController::releaseAssistModeKind(GameModeKind kind) {
    return EbAssistInputGate::releaseAssistModeKind(kind);
}

bool EbAssistController::releaseAssistModePlayer(PlayerObject* player) {
    return EbAssistInputGate::releaseAssistModePlayer(player);
}

bool EbAssistController::shouldPlanRelease(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                         int effectTick) const {
    return EbAssistInputGate::shouldPlanRelease(*this, playLayer, player, player1, effectTick);
}

bool EbAssistController::releaseGateSelfCheck(std::string& report) {
    return EbAssistInputGate::releaseGateSelfCheck(report);
}

int EbAssistController::resolveEffectTick(int attemptTick, bool midStep, bool continuous) {
    return EbAssistTimingRules::resolveEffectTick(attemptTick, midStep, continuous);
}

bool EbAssistController::effectTickSelfCheck(std::string& report) {
    return EbAssistTimingRules::effectTickSelfCheck(report);
}

int EbAssistController::resolveFireTick(int effectTick, int delay, int jumpTick, bool requireOrb) {
    return EbAssistTimingRules::resolveFireTick(effectTick, delay, jumpTick, requireOrb);
}

bool EbAssistController::shouldFireBuffered(int attemptTick, int fireTick, int pressTick,
                                          int planDelay, int limit) {
    return EbAssistTimingRules::shouldFireBuffered(attemptTick, fireTick, pressTick, planDelay,
                                                 limit);
}

bool EbAssistController::fireTickSelfCheck(std::string& report) {
    return EbAssistTimingRules::fireTickSelfCheck(report);
}

int EbAssistController::resolveQueuedHoldTicks(bool requireHold, bool continuous, int releaseAfter,
                                             int releaseEarliest, int releaseLatest,
                                             int extendCap) {
    return EbAssistTimingRules::resolveQueuedHoldTicks(requireHold, continuous, releaseAfter,
                                                     releaseEarliest, releaseLatest, extendCap);
}

bool EbAssistController::queuedHoldSelfCheck(std::string& report) {
    return EbAssistTimingRules::queuedHoldSelfCheck(report);
}

bool EbAssistController::shouldArmHoldUntilSafe(EbSimPlan const& plan, bool trustOk) {
    return EbHoldUntilSafePolicy::shouldArmHoldUntilSafe(plan, trustOk);
}

int EbAssistController::holdUntilSafeEtaTicks(EbSimPlan const& plan, int limit) {
    return EbHoldUntilSafePolicy::holdUntilSafeEtaTicks(plan, limit);
}

bool EbAssistController::holdUntilSafeSelfCheck(std::string& report) {
    return EbHoldUntilSafePolicy::holdUntilSafeSelfCheck(report);
}

bool EbAssistController::shouldReplanHoldUntilSafe(int attemptTick, int lastReplanTick, float x,
                                                 float lastReplanX, bool foresightNow,
                                                 bool foresightLast, int remainingTicks) {
    return EbHoldUntilSafePolicy::shouldReplanHoldUntilSafe(
        attemptTick, lastReplanTick, x, lastReplanX, foresightNow, foresightLast, remainingTicks);
}

bool EbAssistController::armHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                        int effectTick, int limit, EbSimPlan const& plan, bool down) {
    return EbAssistHoldRuntime::armHoldUntilSafe(*this, playLayer, player, player1, effectTick, limit,
                                               plan, down);
}

void EbAssistController::tickHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player) {
    return EbAssistHoldRuntime::tickHoldUntilSafe(*this, playLayer, player);
}

int EbAssistController::estimateRobustK(PlayerObject* player, float dtVariance) {
    int forced = EbStageConfig::robustK();
    if (forced > 0)
        return std::clamp(forced, 1, 3);
    // Recheck path: treat as spam-dense when recent presses look rhythmic.
    bool spamDense = false;
    if (player) {
        auto* playLayer = PlayLayer::get();
        int const slot = (playLayer && player == playLayer->m_player2) ? 1 : 0;
        spamDense = EbClickIntentGuard::get().shouldPassThrough(
            slot, EbAssistController::get().attemptTick(), EbStageConfig::kindOf(player));
    }
    return EbShadowSimulator::autoRobustK(player, dtVariance, spamDense);
}

int EbAssistController::acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire, int absMax,
                                         int robustK, int deadline, bool requireOrb, bool planOk) {
    return EbAssistTimingRules::acceptRecheckSlide(oldFire, attemptTick, proposedFire, absMax,
                                                 robustK, deadline, requireOrb, planOk);
}

bool EbAssistController::recheckSlideSelfCheck(std::string& report) {
    return EbAssistTimingRules::recheckSlideSelfCheck(report);
}

int EbAssistController::activeLimit(PlayerObject* player) const {
    return EbAssistInputGate::activeLimit(*this, player);
}

bool EbAssistController::shouldAssistPlayer(PlayerObject* player) const {
    return EbAssistInputGate::shouldAssistPlayer(*this, player);
}

void EbAssistController::pruneClickTimes() {
    return EbAssistInputGate::pruneClickTimes(*this);
}

void EbAssistController::noteUserClick() {
    return EbAssistInputGate::noteUserClick(*this);
}

int EbAssistController::currentCps() const {
    return EbAssistInputGate::currentCps(*this);
}

bool EbAssistController::cpsLimited() const {
    return EbAssistInputGate::cpsLimited(*this);
}

void EbAssistController::updateOverlay() {
    return EbAssistOverlay::updateOverlay(*this);
}

void EbAssistController::inject(GJBaseGameLayer* layer, bool down) {
    return EbAssistBufferRuntime::inject(*this, layer, down);
}

void EbAssistController::noteRealJump(PlayerObject* player) {
    return EbAssistOutcomes::noteRealJump(*this, player);
}

void EbAssistController::recordDecision(PlayLayer* playLayer, PlayerObject* player, bool down,
                                      EbSimPlan const& plan, int effectTick, int appliedDelay,
                                      int limit) {
    return EbAssistOutcomes::recordDecision(*this, playLayer, player, down, plan, effectTick,
                                          appliedDelay, limit);
}

void EbAssistController::tickLearning(PlayLayer* playLayer, PlayerObject* player) {
    return EbAssistOutcomes::tickLearning(*this, playLayer, player);
}

void EbAssistController::runPostmortem(PlayLayer* playLayer, PlayerObject* player, GameObject* object,
                                     EbDecisionEntry& dec) {
    return EbAssistOutcomes::runPostmortem(*this, playLayer, player, object, dec);
}

void EbAssistController::beginOutcome(EbSimPlan const& plan, int effectTick) {
    return EbAssistOutcomes::beginOutcome(*this, plan, effectTick);
}

void EbAssistController::pollOutcome(PlayLayer* playLayer) {
    return EbAssistOutcomes::pollOutcome(*this, playLayer);
}

void EbAssistController::logPlan(EbSimPlan const& plan, char const* kind) {
    return EbAssistOutcomes::logPlan(*this, plan, kind);
}

void EbAssistController::noteVerdict(AssistReason reason, char const* where) {
    return EbAssistOutcomes::noteVerdict(*this, reason, where);
}

void EbAssistController::onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object) {
    return EbAssistOutcomes::onRealDeath(*this, playLayer, player, object);
}

void EbAssistController::flushPendingRelease(PlayLayer* playLayer) {
    return EbAssistBufferRuntime::flushPendingRelease(*this, playLayer);
}

void EbAssistController::fireBuffered(PlayLayer* playLayer, PlayerObject* player, bool allowRecheck) {
    return EbAssistBufferRuntime::fireBuffered(*this, playLayer, player, allowRecheck);
}

void EbAssistController::onPhysicsTick(PlayLayer* playLayer, float deltaTime) {
    return EbAssistInputRuntime::onPhysicsTick(*this, playLayer, deltaTime);
}

bool EbAssistController::tryBakeAssist(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                     bool down, int effectTick, float firstDt, bool& handled,
                                     EbPressSimBudget* sharedBudget, bool* verifyAttempted,
                                     EbHybridPressReuse* reuse) {
    return EbAssistBakeReuse::tryBakeAssist(*this, playLayer, player, player1, down, effectTick,
                                          firstDt, handled, sharedBudget, verifyAttempted, reuse);
}

bool EbAssistController::planPress(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                 int effectTick, float firstDt,
                                 EbDevClockContract::Stamp const& clockIn) {
    return EbAssistPressPlanner::planPress(*this, playLayer, player, player1, effectTick, firstDt,
                                         clockIn);
}

bool EbAssistController::planRelease(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                   int effectTick, float firstDt,
                                   EbDevClockContract::Stamp const& clockIn) {
    return EbAssistReleasePlanner::planRelease(*this, playLayer, player, player1, effectTick, firstDt,
                                             clockIn);
}

bool EbAssistController::onHandleButton(GJBaseGameLayer* layer, bool down, int button, bool player1) {
    return EbAssistInputRuntime::onHandleButton(*this, layer, down, button, player1);
}

bool EbAssistController::allowPushButton(PlayerObject* player, PlayerButton button) {
    return EbAssistInputRuntime::allowPushButton(*this, player, button);
}
