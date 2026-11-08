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

int AssistControllerImplementation::roundBias(float bias) {
    if (!(bias >= 0.f)) {
        return static_cast<int>(std::lround(bias));
    }
    return static_cast<int>(std::lround(bias));
}

void AssistControllerImplementation::fillPlanRobustLatency(PlanOptions& opts, PlayerObject* player,
                                                           float dtVariance, bool spamDense) {
    opts.robustK = StageConfig::robustK();
    opts.robustJitter = StageConfig::robustJitter();
    opts.dtVariance = dtVariance;
    opts.spamDense = spamDense;
    if (player) {
        opts.latencyWeight = StageConfig::paramsFor(player).latencyWeight;
        // Wave λ=1.0 prefers earliest hard — in spam+orb assists that phase-shifts
        // into spikes. Soften only on spam-dense assists (Pass-excluded paths).
        if (spamDense && player->m_isDart && opts.latencyWeight > 0.5f) {
            opts.latencyWeight *= 0.65f;
        }
    }
}

// Map a nearby BakeMap window onto delay ticks for Live prefer-band ordering.
// Absolute bake frames → live ticks (unmapTick), then delays relative to effectTick.
void AssistControllerImplementation::fillBakePreferBandFromReuse(PlanOptions& opts,
                                                                 HybridPressReuse const& reuse,
                                                                 int effectTick, int limit,
                                                                 float simDt) {
    if (!reuse.hasWindow || limit <= 0 || simDt <= 0.f)
        return;
    double const tps = reuse.bakeTps > 0.0 ? reuse.bakeTps : AssistClock::kNominalTps;
    auto& clock = AssistClock::get();
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

void AssistControllerImplementation::fillBakePreferBand(PlanOptions& opts, PlayerObject* player,
                                                        bool player1, bool down, int effectTick,
                                                        int limit, int attemptTick, float simDt,
                                                        HybridPressReuse const* reuse) {
    if (reuse && reuse->hasWindow) {
        AssistControllerImplementation::fillBakePreferBandFromReuse(opts, *reuse, effectTick, limit,
                                                                    simDt);
        return;
    }
    if (!player || limit <= 0 || simDt <= 0.f)
        return;
    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty())
        return;

    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    int const approxFrame = AssistClock::get().remapTick(tps, attemptTick, simDt);
    auto const* win = bake.findNear(player->getPositionX(), approxFrame, down, !player1,
                                    StageConfig::kindOf(player));
    if (!win)
        return;

    HybridPressReuse tmp;
    tmp.hasWindow = true;
    tmp.earliest = win->earliest;
    tmp.latest = win->latest;
    tmp.nominalFrame = win->nominalFrame;
    tmp.bakeTps = tps;
    AssistControllerImplementation::fillBakePreferBandFromReuse(opts, tmp, effectTick, limit,
                                                                simDt);
}

// Nearby bake click → ClickIntentGuard must not Pass (assist allowed).
// Hybrid callers clear this after bake verify rejects the window (avoid forcing
// full Live on every moderate-CPS click near a stale bake).
bool AssistControllerImplementation::nearBakeWindowHint(PlayerObject* player, bool player1,
                                                        bool down, int attemptTick, float simDt,
                                                        HybridPressReuse const* reuse) {
    if (reuse)
        return reuse->hasWindow;
    if (!player || simDt <= 0.f)
        return false;
    auto const& bake = BakeMap::get();
    if (!bake.loaded() || bake.data().clicks.empty())
        return false;
    double tps = bake.data().tps > 0.0 ? bake.data().tps : AssistClock::kNominalTps;
    int const approxFrame = AssistClock::get().remapTick(tps, attemptTick, simDt);
    return bake.findNear(player->getPositionX(), approxFrame, down, !player1,
                         StageConfig::kindOf(player)) != nullptr;
}

// Hybrid Live fallthrough: never HUS-inject before bake nominal (press or release).
// Returns true when caller should Pass instead of armHoldUntilSafe.
bool AssistControllerImplementation::husBlockedBeforeBakeNominal(HybridPressReuse const& reuse,
                                                                 int effectTick, float simDt,
                                                                 int& outNomTick) {
    outNomTick = -1;
    if (!reuse.hasWindow || simDt <= 0.f)
        return false;
    double const bakeTps = reuse.bakeTps > 0.0 ? reuse.bakeTps : AssistClock::kNominalTps;
    outNomTick = AssistClock::get().unmapTick(bakeTps, reuse.nominalFrame, simDt);
    return effectTick < outNomTick;
}

bool AssistControllerImplementation::nearOrbOrPadHint(PlayLayer* playLayer, PlayerObject* player,
                                                      int limit, float simDt) {
    if (!StageConfig::assistOrbs())
        return false;
    if (!(!playLayer || !player || limit <= 0 || simDt <= 0.f)) {
        return ShadowSimulator::spatialOrbHint(playLayer, player, limit, simDt);
    }
    return false;
}

AssistController& AssistController::get() {
    static AssistController instance;
    return instance;
}

bool AssistController::isInjecting() const {
    return m_injecting;
}
BufferedInput const& AssistController::buffer() const {
    return m_buffer;
}
int AssistController::attemptTick() const {
    return m_attemptTick;
}
float AssistController::lastDt() const {
    return m_lastDt;
}

double AssistController::estimatedTps() const {
    return AssistClock::get().simTpsFromDt(this->m_simDt);
}

double AssistController::wallTps() const {
    return AssistClock::get().wallTpsFromDt(this->m_lastDt);
}

std::string AssistController::overlayText() const {
    return m_overlayText;
}

void AssistController::clearBuffer() {
    m_buffer = {};
}

void AssistController::onCheckpointLoad() {
    return AssistLifecycle::onCheckpointLoad(*this);
}

void AssistController::onLevelComplete(PlayLayer* playLayer) {
    return AssistLifecycle::onLevelComplete(*this, playLayer);
}

void AssistController::syncButtonState(PlayLayer* playLayer) {
    return AssistLifecycle::syncButtonState(*this, playLayer);
}

void AssistController::resetAttempt(PlayLayer* playLayer) {
    return AssistLifecycle::resetAttempt(*this, playLayer);
}

void AssistController::onLevelInit(PlayLayer* playLayer) {
    return AssistLifecycle::onLevelInit(*this, playLayer);
}

void AssistController::onLevelQuit() {
    return AssistLifecycle::onLevelQuit(*this);
}

geode::Result<> AssistController::ensureStartSnapshot(PlayLayer* layer) {
    return AssistLifecycle::ensureStartSnapshot(*this, layer);
}

geode::Result<int> AssistController::runBakeMap(PlayLayer* layer, bool bruteForce) {
    return AssistLifecycle::runBakeMap(*this, layer, bruteForce);
}

void AssistController::resetLevelProfile() {
    return AssistLifecycle::resetLevelProfile(*this);
}

void AssistController::beginRealUpdate() {
    return AssistLifecycle::beginRealUpdate(*this);
}

void AssistController::endRealUpdate() {
    return AssistLifecycle::endRealUpdate(*this);
}

void AssistController::onSubStep(PlayerObject* player, float deltaTime) {
    return AssistLifecycle::onSubStep(*this, player, deltaTime);
}

bool AssistController::continuousModeKind(GameModeKind kind) {
    return AssistInputGate::continuousModeKind(kind);
}

bool AssistController::continuousModePlayer(PlayerObject* player) {
    return AssistInputGate::continuousModePlayer(player);
}

bool AssistController::releaseAssistModeKind(GameModeKind kind) {
    return AssistInputGate::releaseAssistModeKind(kind);
}

bool AssistController::releaseAssistModePlayer(PlayerObject* player) {
    return AssistInputGate::releaseAssistModePlayer(player);
}

bool AssistController::shouldPlanRelease(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                         int effectTick) const {
    return AssistInputGate::shouldPlanRelease(*this, playLayer, player, player1, effectTick);
}

bool AssistController::releaseGateSelfCheck(std::string& report) {
    return AssistInputGate::releaseGateSelfCheck(report);
}

int AssistController::resolveEffectTick(int attemptTick, bool midStep, bool continuous) {
    return AssistTimingRules::resolveEffectTick(attemptTick, midStep, continuous);
}

bool AssistController::effectTickSelfCheck(std::string& report) {
    return AssistTimingRules::effectTickSelfCheck(report);
}

int AssistController::resolveFireTick(int effectTick, int delay, int jumpTick, bool requireOrb) {
    return AssistTimingRules::resolveFireTick(effectTick, delay, jumpTick, requireOrb);
}

bool AssistController::shouldFireBuffered(int attemptTick, int fireTick, int pressTick,
                                          int planDelay, int limit) {
    return AssistTimingRules::shouldFireBuffered(attemptTick, fireTick, pressTick, planDelay,
                                                 limit);
}

bool AssistController::fireTickSelfCheck(std::string& report) {
    return AssistTimingRules::fireTickSelfCheck(report);
}

int AssistController::resolveQueuedHoldTicks(bool requireHold, bool continuous, int releaseAfter,
                                             int releaseEarliest, int releaseLatest,
                                             int extendCap) {
    return AssistTimingRules::resolveQueuedHoldTicks(requireHold, continuous, releaseAfter,
                                                     releaseEarliest, releaseLatest, extendCap);
}

bool AssistController::queuedHoldSelfCheck(std::string& report) {
    return AssistTimingRules::queuedHoldSelfCheck(report);
}

bool AssistController::shouldArmHoldUntilSafe(SimPlan const& plan, bool trustOk) {
    return HoldUntilSafePolicy::shouldArmHoldUntilSafe(plan, trustOk);
}

int AssistController::holdUntilSafeEtaTicks(SimPlan const& plan, int limit) {
    return HoldUntilSafePolicy::holdUntilSafeEtaTicks(plan, limit);
}

bool AssistController::holdUntilSafeSelfCheck(std::string& report) {
    return HoldUntilSafePolicy::holdUntilSafeSelfCheck(report);
}

bool AssistController::shouldReplanHoldUntilSafe(int attemptTick, int lastReplanTick, float x,
                                                 float lastReplanX, bool foresightNow,
                                                 bool foresightLast, int remainingTicks) {
    return HoldUntilSafePolicy::shouldReplanHoldUntilSafe(
        attemptTick, lastReplanTick, x, lastReplanX, foresightNow, foresightLast, remainingTicks);
}

bool AssistController::armHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                        int effectTick, int limit, SimPlan const& plan, bool down) {
    return AssistHoldRuntime::armHoldUntilSafe(*this, playLayer, player, player1, effectTick, limit,
                                               plan, down);
}

void AssistController::tickHoldUntilSafe(PlayLayer* playLayer, PlayerObject* player) {
    return AssistHoldRuntime::tickHoldUntilSafe(*this, playLayer, player);
}

int AssistController::estimateRobustK(PlayerObject* player, float dtVariance) {
    int forced = StageConfig::robustK();
    if (forced > 0)
        return std::clamp(forced, 1, 3);
    // Recheck path: treat as spam-dense when recent presses look rhythmic.
    bool spamDense = false;
    if (player) {
        auto* playLayer = PlayLayer::get();
        int const slot = (playLayer && player == playLayer->m_player2) ? 1 : 0;
        spamDense = ClickIntentGuard::get().shouldPassThrough(
            slot, AssistController::get().attemptTick(), StageConfig::kindOf(player));
    }
    return ShadowSimulator::autoRobustK(player, dtVariance, spamDense);
}

int AssistController::acceptRecheckSlide(int oldFire, int attemptTick, int proposedFire, int absMax,
                                         int robustK, int deadline, bool requireOrb, bool planOk) {
    return AssistTimingRules::acceptRecheckSlide(oldFire, attemptTick, proposedFire, absMax,
                                                 robustK, deadline, requireOrb, planOk);
}

bool AssistController::recheckSlideSelfCheck(std::string& report) {
    return AssistTimingRules::recheckSlideSelfCheck(report);
}

int AssistController::activeLimit(PlayerObject* player) const {
    return AssistInputGate::activeLimit(*this, player);
}

bool AssistController::shouldAssistPlayer(PlayerObject* player) const {
    return AssistInputGate::shouldAssistPlayer(*this, player);
}

void AssistController::pruneClickTimes() {
    return AssistInputGate::pruneClickTimes(*this);
}

void AssistController::noteUserClick() {
    return AssistInputGate::noteUserClick(*this);
}

int AssistController::currentCps() const {
    return AssistInputGate::currentCps(*this);
}

bool AssistController::cpsLimited() const {
    return AssistInputGate::cpsLimited(*this);
}

void AssistController::updateOverlay() {
    return AssistOverlay::updateOverlay(*this);
}

void AssistController::inject(GJBaseGameLayer* layer, bool down) {
    return AssistBufferRuntime::inject(*this, layer, down);
}

void AssistController::noteRealJump(PlayerObject* player) {
    return AssistOutcomes::noteRealJump(*this, player);
}

void AssistController::recordDecision(PlayLayer* playLayer, PlayerObject* player, bool down,
                                      SimPlan const& plan, int effectTick, int appliedDelay,
                                      int limit) {
    return AssistOutcomes::recordDecision(*this, playLayer, player, down, plan, effectTick,
                                          appliedDelay, limit);
}

void AssistController::tickLearning(PlayLayer* playLayer, PlayerObject* player) {
    return AssistOutcomes::tickLearning(*this, playLayer, player);
}

void AssistController::runPostmortem(PlayLayer* playLayer, PlayerObject* player, GameObject* object,
                                     DecisionEntry& dec) {
    return AssistOutcomes::runPostmortem(*this, playLayer, player, object, dec);
}

void AssistController::beginOutcome(SimPlan const& plan, int effectTick) {
    return AssistOutcomes::beginOutcome(*this, plan, effectTick);
}

void AssistController::pollOutcome(PlayLayer* playLayer) {
    return AssistOutcomes::pollOutcome(*this, playLayer);
}

void AssistController::logPlan(SimPlan const& plan, char const* kind) {
    return AssistOutcomes::logPlan(*this, plan, kind);
}

void AssistController::noteVerdict(AssistReason reason, char const* where) {
    return AssistOutcomes::noteVerdict(*this, reason, where);
}

void AssistController::onRealDeath(PlayLayer* playLayer, PlayerObject* player, GameObject* object) {
    return AssistOutcomes::onRealDeath(*this, playLayer, player, object);
}

void AssistController::flushPendingRelease(PlayLayer* playLayer) {
    return AssistBufferRuntime::flushPendingRelease(*this, playLayer);
}

void AssistController::fireBuffered(PlayLayer* playLayer, PlayerObject* player, bool allowRecheck) {
    return AssistBufferRuntime::fireBuffered(*this, playLayer, player, allowRecheck);
}

void AssistController::onPhysicsTick(PlayLayer* playLayer, float deltaTime) {
    return AssistInputRuntime::onPhysicsTick(*this, playLayer, deltaTime);
}

bool AssistController::tryBakeAssist(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                     bool down, int effectTick, float firstDt, bool& handled,
                                     PressSimBudget* sharedBudget, bool* verifyAttempted,
                                     HybridPressReuse* reuse) {
    return AssistBakeReuse::tryBakeAssist(*this, playLayer, player, player1, down, effectTick,
                                          firstDt, handled, sharedBudget, verifyAttempted, reuse);
}

bool AssistController::planPress(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                 int effectTick, float firstDt,
                                 DevClockContract::Stamp const& clockIn) {
    return AssistPressPlanner::planPress(*this, playLayer, player, player1, effectTick, firstDt,
                                         clockIn);
}

bool AssistController::planRelease(PlayLayer* playLayer, PlayerObject* player, bool player1,
                                   int effectTick, float firstDt,
                                   DevClockContract::Stamp const& clockIn) {
    return AssistReleasePlanner::planRelease(*this, playLayer, player, player1, effectTick, firstDt,
                                             clockIn);
}

bool AssistController::onHandleButton(GJBaseGameLayer* layer, bool down, int button, bool player1) {
    return AssistInputRuntime::onHandleButton(*this, layer, down, button, player1);
}

bool AssistController::allowPushButton(PlayerObject* player, PlayerButton button) {
    return AssistInputRuntime::allowPushButton(*this, player, button);
}
