#include "assist_controller.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../../dev/capture/dev_clock_contract.hpp"
#include "../bake/bake_map.hpp"
#include "../bake/bake_verifier.hpp"
#include "../diagnostics/decision_log.hpp"
#include "../diagnostics/divergence_tracker.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/shadow_simulator.hpp"
#include "../sim/sim_trust.hpp"
#include "assist_clock.hpp"
#include "contracts/assist_timing_rules.hpp"
#include "contracts/assist_verdict.hpp"
#include "input/assist_input_gate.hpp"
#include "input/click_intent_guard.hpp"
#include "input/hold_until_safe_policy.hpp"
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

void EbAssistControllerImplementation::fillPlanRobustLatency(EbPlanOptions& opts,
                                                             PlayerObject* player, float dtVariance,
                                                             bool spamDense) {
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
                                          int effectTick, int limit, EbSimPlan const& plan,
                                          bool down) {
    return EbAssistHoldRuntime::armHoldUntilSafe(*this, playLayer, player, player1, effectTick,
                                                 limit, plan, down);
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

int EbAssistController::acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire,
                                           int absMax, int robustK, int deadline, bool requireOrb,
                                           bool planOk) {
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

void EbAssistController::runPostmortem(PlayLayer* playLayer, PlayerObject* player,
                                       GameObject* object, EbDecisionEntry& dec) {
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

void EbAssistController::onRealDeath(PlayLayer* playLayer, PlayerObject* player,
                                     GameObject* object) {
    return EbAssistOutcomes::onRealDeath(*this, playLayer, player, object);
}

void EbAssistController::flushPendingRelease(PlayLayer* playLayer) {
    return EbAssistBufferRuntime::flushPendingRelease(*this, playLayer);
}

void EbAssistController::fireBuffered(PlayLayer* playLayer, PlayerObject* player,
                                      bool allowRecheck) {
    return EbAssistBufferRuntime::fireBuffered(*this, playLayer, player, allowRecheck);
}

void EbAssistController::onPhysicsTick(PlayLayer* playLayer, float deltaTime) {
    return EbAssistInputRuntime::onPhysicsTick(*this, playLayer, deltaTime);
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
    return EbAssistReleasePlanner::planRelease(*this, playLayer, player, player1, effectTick,
                                               firstDt, clockIn);
}

bool EbAssistController::onHandleButton(GJBaseGameLayer* layer, bool down, int button,
                                        bool player1) {
    return EbAssistInputRuntime::onHandleButton(*this, layer, down, button, player1);
}

bool EbAssistController::allowPushButton(PlayerObject* player, PlayerButton button) {
    return EbAssistInputRuntime::allowPushButton(*this, player, button);
}

void EbAssistController::acknowledgeInput(bool player1, bool down, int button) {
    if (button != EbAssistControllerImplementation::JUMP_BUTTON)
        return;
    if (m_injecting)
        m_flightCorrectionObserved[player1 ? 0 : 1] = true;
    m_inputReconciler.acknowledge(player1 ? 0 : 1, down,
                                  std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
}

void EbAssistController::measureFlight(PlayerObject* player, float deltaTime) {
    auto* layer = PlayLayer::get();
    if (!layer || !player)
        return;
    int const slot = player == layer->m_player1 ? 0 : player == layer->m_player2 ? 1 : -1;
    if (slot < 0)
        return;
    EbFlightMeasurement::Sample sample;
    sample.x = player->m_position.x;
    sample.y = player->m_position.y;
    sample.dt = EbAssistClock::gdStepToSeconds(deltaTime);
    sample.mode = static_cast<int>(EbStageConfig::kindOf(player));
    sample.speed = player->m_playerSpeed;
    sample.size = player->m_vehicleSize;
    sample.timeWarp = layer->m_gameState.m_timeWarp;
    sample.gravity = player->m_gravity;
    sample.inverted = player->m_isUpsideDown;
    sample.sideways = player->m_isSideways;
    sample.reversed = player->m_isGoingLeft;
    sample.valid = !player->m_isDead && !player->m_isPlatformer;
    m_flightContext[slot] = sample;
    m_flightMeasurement[slot].observe(sample);
    bool const buffered = m_buffer.state != BufferState::Idle && m_buffer.player1 == (slot == 0);
    bool const pendingRelease = m_pendingRelease && m_pendingReleaseP1 == (slot == 0);
    bool const assisted = m_flightCorrectionObserved[slot] || buffered || pendingRelease;
    bool const supported = (player->m_isShip || player->m_isDart || player->m_isSwing) &&
                           !player->m_controlsDisabled && !player->m_isDashing &&
                           !EbSimTrust::get().parityDrift() &&
                           !EbDivergenceTracker::get().simUntrusted();
    m_flightIntent[slot].observe(m_flightMeasurement[slot].result(), sample.dt, supported,
                                 assisted);
    m_flightCorrectionObserved[slot] = false;
    auto const& intent = m_flightIntent[slot].result();
    if (intent.targetValid && intent.state == EbFlightIntent::State::Tracking) {
        if (!m_shipLineHasAnchor[slot]) {
            m_shipLineX[slot] = sample.x;
            m_shipLineY[slot] = sample.y;
            m_shipLineHasAnchor[slot] = true;
        }
    } else {
        m_shipLineHasAnchor[slot] = false;
    }
}
EbFlightMeasurement::Result EbAssistController::flightMeasurement(int player) const {
    if (player < 0 || player >= 2)
        return {};
    return m_flightMeasurement[player].result();
}

EbFlightIntent::Result EbAssistController::flightIntent(int player) const {
    if (player < 0 || player >= 2)
        return {};
    return m_flightIntent[player].result();
}
EbAssistController::StraightflyView EbAssistController::straightflyView(int player) const {
    StraightflyView view;
    if (player < 0 || player >= 2 || !m_shipLineHasAnchor[player])
        return view;
    auto const& intent = m_flightIntent[player].result();
    if (!intent.targetValid || intent.state != EbFlightIntent::State::Tracking)
        return view;
    auto const& context = m_flightContext[player];
    if (!context.valid)
        return view;
    view.valid = true;
    view.sideways = context.sideways;
    view.reversed = context.reversed;
    view.x = m_shipLineX[player];
    view.y = m_shipLineY[player];
    view.degrees = intent.targetDegrees;
    view.bodySize = 30. * (context.size > 0.f ? context.size : 1.f);
    view.confidence = intent.confidence;
    return view;
}
void EbAssistController::configureFlightIntent(bool manualTarget, double degrees) {
    bool const restoreEnabled = m_shipLineEnabled;
    if (restoreEnabled)
        setShipLineEnabled(false);
    EbFlightIntent::Settings settings;
    settings.manualTarget = manualTarget;
    settings.targetDegrees = degrees;
    for (auto& intent : m_flightIntent)
        intent.configure(settings);
    m_shipLineEnabled = restoreEnabled;
}

void EbAssistController::setShipLineEnabled(bool enabled) {
    if (!enabled && m_buffer.shipLineOwned && m_buffer.state == BufferState::Buffered) {
        int const slot = m_buffer.player1 ? 0 : 1;
        clearBuffer();
        auto* layer = PlayLayer::get();
        if (layer && m_inputReconciler.state(slot).dispatched != m_userHolding[slot]) {
            m_injecting = true;
            layer->handleButton(m_userHolding[slot], EbAssistControllerImplementation::JUMP_BUTTON,
                                slot == 0);
            m_injecting = false;
        }
    }
    m_shipLineEnabled = enabled;
    if (!enabled) {
        for (auto& intent : m_flightIntent)
            intent.reset();
        m_shipLineHasAnchor[0] = m_shipLineHasAnchor[1] = false;
    }
}
bool EbAssistController::shipLineEnabled() const {
    return m_shipLineEnabled;
}
void EbAssistController::fillShipLineOptions(EbPlanOptions& options, PlayLayer* layer,
                                             PlayerObject* player) {
    if (!m_shipLineEnabled || !EbStageConfig::assistPress() || !EbStageConfig::assistRelease() ||
        !layer || !player || !player->m_isShip || player->m_isDashing ||
        layer->m_gameState.m_isDualMode || player->m_controlsDisabled)
        return;
    int const slot = player == layer->m_player1 ? 0 : player == layer->m_player2 ? 1 : -1;
    if (slot < 0)
        return;
    auto const& context = m_flightContext[slot];
    if (!context.valid || context.mode != static_cast<int>(EbStageConfig::kindOf(player)) ||
        context.inverted != player->m_isUpsideDown || context.sideways != player->m_isSideways ||
        context.reversed != player->m_isGoingLeft || context.speed != player->m_playerSpeed ||
        context.size != player->m_vehicleSize || context.gravity != player->m_gravity ||
        context.timeWarp != layer->m_gameState.m_timeWarp)
        return;
    auto const intent = m_flightIntent[slot].result();
    if (!m_shipLineHasAnchor[slot] || !intent.targetValid ||
        intent.state != EbFlightIntent::State::Tracking || intent.confidence < .35)
        return;
    if (EbAssistControllerImplementation::nearOrbOrPadHint(layer, player, activeLimit(player),
                                                           m_simDt))
        return;
    options.shipLine.enabled = true;
    options.shipLine.x = m_shipLineX[slot];
    options.shipLine.y = m_shipLineY[slot];
    options.shipLine.degrees = intent.targetDegrees;
    options.shipLine.bodySize = 30. * player->m_vehicleSize;
    options.shipLine.reversed = player->m_isGoingLeft;
    options.shipLine.sideways = player->m_isSideways;
    options.smartDepth = 1;
}
bool EbAssistController::yieldShipLine(PlayLayer* layer, PlayerObject* player, bool down) {
    if (!m_shipLineEnabled || !layer || !player || !player->m_isShip)
        return false;
    int const slot = player == layer->m_player1 ? 0 : player == layer->m_player2 ? 1 : -1;
    if (slot < 0)
        return false;
    bool const ours = m_buffer.state == BufferState::Buffered && m_buffer.shipLineOwned &&
                      m_buffer.player1 == (slot == 0);
    if (ours) {
        bool const delayedDown = m_buffer.down;
        clearBuffer();
        if (!delayedDown && down) {
            m_injecting = true;
            layer->handleButton(false, EbAssistControllerImplementation::JUMP_BUTTON, slot == 0);
            m_injecting = false;
        } else if (delayedDown && !down) {
            return true;
        }
    }
    return ours || m_flightIntent[slot].result().yieldRequested;
}
