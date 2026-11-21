#include "hybrid_learning.hpp"
#include "../../config/assist_policy_settings.hpp"
#include "../../debug/assist_log.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/sim_trust.hpp"
#include <algorithm>
#include <cmath>

EbHybridLearning& EbHybridLearning::get() {
    static EbHybridLearning instance;
    return instance;
}
bool EbHybridLearning::enabled() {
    return EbStageConfig::assistMode() == EbStageConfig::AssistMode::Hybrid &&
           EbStageConfig::autoTuneEnabled();
}
bool EbHybridLearning::tablesReadable() {
    auto const mode = EbStageConfig::assistMode();
    return mode == EbStageConfig::AssistMode::Hybrid || mode == EbStageConfig::AssistMode::AI;
}
void EbHybridLearning::open(std::filesystem::path path) {
    close();
    m_path = std::move(path);
    settingsChanged();
}
void EbHybridLearning::applyCapacityLimits() {
    m_model.setMaxUniqueKeys(maxUniqueKeys);
    m_model.setMaxActionsPerSpot(maxActionsPerSpot);
    m_model.setUcbCoefficient(ucbCoefficient);
    m_model.setNeighborBleed(neighborBleed);
}
void EbHybridLearning::settingsChanged() {
    boundary(false);
    applyCapacityLimits();
    if (m_path.empty() || m_loaded)
        return;
    auto const mode = EbStageConfig::assistMode();
    // Hybrid keeps Auto-Tune gate for opening tables; AI loads for bootstrap even Frozen.
    if (mode == EbStageConfig::AssistMode::Hybrid && !EbStageConfig::autoTuneEnabled())
        return;
    if (mode != EbStageConfig::AssistMode::Hybrid && mode != EbStageConfig::AssistMode::AI)
        return;
    (void)m_model.load(m_path);
    m_loaded = true;
}
void EbHybridLearning::close() {
    save();
    m_path.clear();
    m_loaded = false;
    m_model.reset();
    edges = observations = tried = confirmed = drift = verifyUpdates = 0;
    boundary(false);
}
void EbHybridLearning::reset() {
    if (m_path.empty())
        return;
    m_loaded = true;
    m_model.reset();
    edges = observations = tried = confirmed = drift = verifyUpdates = 0;
    boundary(false);
    save();
}
void EbHybridLearning::save() {
    if (m_loaded && !m_path.empty() && !m_model.save(m_path))
        EbAssistLog::info("Hybrid: could not save level learning");
}
void EbHybridLearning::boundary(bool newAttempt) {
    m_previous = {};
    m_intervening = 0;
    m_sequences = {};
    m_edges = {};
    m_lastTick.fill(-1);
    m_interval.fill(-1);
    m_previousInterval.fill(-1);
    m_trace = {};
    m_traceWrite = 0;
    m_traceCount = 0;
    m_pending = false;
    if (newAttempt)
        m_model.nextAttempt();
}
void EbHybridLearning::pushTrace(EbHybridObservation const& edge) {
    if (!edge.key || edge.delay < 0)
        return;
    m_trace[static_cast<size_t>(m_traceWrite)] = edge;
    m_traceWrite = (m_traceWrite + 1) % static_cast<int>(m_trace.size());
    m_traceCount = std::min(static_cast<int>(m_trace.size()), m_traceCount + 1);
}
void EbHybridLearning::creditEligibility(int tick) {
    int const depth = std::clamp(eligibilitySteps, 0, static_cast<int>(m_trace.size()));
    if (depth <= 0)
        return;
    for (int step = 1; step <= depth && step <= m_traceCount; ++step) {
        int const index =
            (m_traceWrite - step + static_cast<int>(m_trace.size())) % static_cast<int>(m_trace.size());
        auto const& prior = m_trace[static_cast<size_t>(index)];
        if (!prior.key || !prior.delivered || prior.delay < 0)
            continue;
        int const priorAge = tick - prior.tick;
        if (tick < prior.tick || priorAge > prior.horizon)
            continue;
        // Decaying credit: most recent prior edge gets a softer death penalty.
        int const soft = std::max(-2, -deathPenalty + step - 1);
        if (!soft)
            continue;
        feedback(prior, soft, priorAge, 0, false);
        ++observations;
    }
}
int EbHybridLearning::shapeReward(EbHybridObservation const& edge, int reward, int deathAge,
                                  bool survivedBaseline) const {
    if (!reward)
        return 0;
    int shaped = reward == 1 ? survivalReward : reward;
    if (shaped > 0) {
        // Prefer tighter delays among ordinary survivals; rescues keep full credit.
        if (shaped == survivalReward && edge.delay > 8)
            shaped = std::max(1, shaped - (edge.delay > 16 ? 1 : 0));
        if (survivedBaseline && edge.delay > 0)
            shaped = std::max(shaped, 3);
        return std::clamp(shaped, 1, 3);
    }
    // Early death right after the assisted fire is stronger negative evidence.
    if (deathAge >= 0 && edge.delivered && deathAge <= edge.delay + 4)
        shaped = std::max(-4, shaped - 1);
    return std::clamp(shaped, -4, -1);
}
void EbHybridLearning::edge(bool player1, int tick, bool down) {
    if (!enabled() || !m_loaded)
        return;
    ++edges;
    int slot = player1 ? 0 : 1;
    if (trackOutcomes && trackSequence)
        m_sequences[slot] = ((m_sequences[slot] << 1) | int(down)) & 255;
    if (m_previous.key)
        m_intervening = std::min(65535, m_intervening + 1);
    // A following physical edge ends causal attribution for both players.
    if (m_pending) {
        ++interrupted;
        // Score only the interval before the next physical input. Later survival
        // belongs to the changed input sequence and cannot prove this action.
        for (auto const& previous : m_edges) {
            if (previous.delivered && previous.key && trackOutcomes) {
                m_previous = previous;
                m_intervening = 1;
            }
            int const age = tick - previous.tick;
            if (!previous.delivered || previous.delay < 0 || age < previous.delay + 8)
                continue;
            bool const ringVerified = previous.needRing && previous.activation;
            bool const deathPassed =
                previous.predictedDeath && age > previous.baselineDeathTick + 1;
            bool const landingVerified = previous.ballLanding && previous.landed;
            if (previous.needRing && !ringVerified)
                continue;
            int const reward = ringVerified || deathPassed || landingVerified ? 3 : 1;
            feedback(previous, reward, -1, 0, deathPassed);
            ++observations;
        }
    }
    m_edges = {};
    m_pending = false;
    m_previousInterval[slot] = m_interval[slot];
    m_interval[slot] = m_lastTick[slot] < 0 ? -1 : std::max(0, tick - m_lastTick[slot]);
    m_lastTick[slot] = tick;
}
int EbHybridLearning::prepare(PlayLayer* layer, PlayerObject* player, bool down, bool player1,
                              int tick, int limit, float dt, float firstDt,
                              EbPlanOptions const& options) {
    // Read proposals when tables are open (Hybrid train path or AI bootstrap).
    if (!m_loaded || !tablesReadable() || dt <= 0.f)
        return -1;
    applyCapacityLimits();
    int slot = player1 ? 0 : 1;
    auto& edge = m_edges[slot];
    auto kind = EbStageConfig::kindOf(player);
    auto params = EbStageConfig::paramsFor(kind);
    uint64_t key = 1469598103934665603ull;
    auto add = [&](int value) { key = EbHybridModel::mix(key, value); };
    add(static_cast<int>(std::floor(player->getPositionX() / std::max(16, positionBinX))));
    add(static_cast<int>(std::floor(player->getPositionY() / std::max(16, positionBinY))));
    add(static_cast<int>(kind));
    add(down);
    add(player1);
    add(layer->m_gameState.m_isDualMode);
    add(player->m_isUpsideDown);
    add(player->m_isGoingLeft);
    add(player->m_isPlatformer);
    add(layer->m_isPracticeMode);
    add(player->m_isOnGround || player->m_isOnGround2);
    add(std::lround(player->m_playerSpeed * 32));
    add(std::lround(player->m_vehicleSize * 8));
    add((player->m_yVelocity > 1) - (player->m_yVelocity < -1));
    add(std::lround(player->m_gravityMod * 16));
    add(std::lround(player->m_speedMultiplier * 4));
    add(player->m_isSideways);
    add(std::lround((1.f / dt) / std::max(1, tpsQuantum)));
    if (matchSubstep)
        add(firstDt < dt * 0.99f);
    add(std::lround(layer->m_gameState.m_timeWarp * 16));
    // Distinguish short taps and longer holds without requiring tick-perfect repetition.
    if (matchIntervals) {
        add(m_interval[slot] < 0 ? -1 : int(m_interval[slot] > 40));
        add(m_previousInterval[slot] < 0 ? -1 : int(m_previousInterval[slot] > 72));
    }
    uint64_t const pathKey = key;
    // Reconstruct the previous config-dependent key for existing saved profiles.
    bool const hasPath = m_model.hasEvidence(pathKey);
    if (!hasPath) {
        add(limit);
        add(params.cap);
        add(params.slack);
        add(options.smartDepth);
        add(EbStageConfig::lookAheadCap());
        add(EbStageConfig::assistProbePastHorizon());
        add(options.smartHorizon);
        add(options.smartCombos);
        add(options.tickBudget);
        // Remaining frame time varies per click and must never split learned contexts.
        add(EbStageConfig::simBudgetUs());
        add(options.allowSubtick);
        add(options.adaptivePlan);
        add(options.robustK);
        add(options.robustJitter);
        add(options.adaptiveScreenTicks);
        add(options.adaptiveVerifyQuantum);
        add(std::lround(options.latencyWeight * 16));
        add(EbStageConfig::assistOrbs());
        add(static_cast<int>(EbAssistPolicySettings::ground()));
        add(static_cast<int>(EbAssistPolicySettings::chain(kind)));
    }
    edge.phaseBin =
        trackOutcomes && trackPhase ? std::clamp(int((1.f - firstDt / dt) * 8), 0, 7) : -1;
    edge.sequence = trackOutcomes && trackSequence ? m_sequences[slot] : -1;
    edge.key = pathKey;
    if (coarseFallback && (player->m_isShip || player->m_isDart || player->m_isSwing)) {
        uint64_t coarse = EbHybridModel::mix(1469598103934665603ull, 0x434f4152);
        for (int value : {int(std::floor(player->getPositionX() / 128.f)), int(kind),
                          int(player->m_isGoingLeft), int(down), int(player1),
                          int(player->m_isUpsideDown), int(std::lround(player->m_playerSpeed * 32)),
                          int(std::lround(player->m_vehicleSize * 8))})
            coarse = EbHybridModel::mix(coarse, value);
        edge.coarseKey = coarse;
    }
    edge.tick = tick;
    edge.mode = static_cast<int>(kind);
    edge.leftGround = !(player->m_isOnGround || player->m_isOnGround2);
    edge.touchedCount = static_cast<int>(player->m_touchedRings.size());
    edge.touched = player->m_touchedRing;
    edge.hint =
        layer->m_gameState.m_isDualMode ? -1 : m_model.proposal(pathKey, minimumAttempts, limit);
    if (!layer->m_gameState.m_isDualMode && edge.hint < 0 && !hasPath)
        edge.hint = m_model.proposal(key, minimumAttempts, limit);
    if (edge.hint < 0 && edge.coarseKey && !layer->m_gameState.m_isDualMode)
        edge.hint =
            m_model.proposal(edge.coarseKey, std::max(minimumAttempts, coarseAttempts), limit);
    return edge.hint;
}
std::vector<int> EbHybridLearning::rankedVerifyDelays(int limit) const {
    if (!m_loaded || !tablesReadable())
        return {};
    // Prefer the path key from the last prepare(); fall back to coarse if needed.
    for (auto const& edge : m_edges) {
        if (!edge.key)
            continue;
        auto ranked = m_model.rankedDelays(edge.key, minimumAttempts, limit, true);
        if (!ranked.empty())
            return ranked;
        if (edge.coarseKey) {
            ranked = m_model.rankedDelays(edge.coarseKey, std::max(minimumAttempts, coarseAttempts),
                                         limit, true);
            if (!ranked.empty())
                return ranked;
        }
    }
    return {};
}
void EbHybridLearning::noteVerifyOutcome(int delay, bool verifiedSafe) {
    if (!enabled() || !m_loaded || delay < 0)
        return;
    applyCapacityLimits();
    for (auto const& edge : m_edges) {
        if (!edge.key)
            continue;
        m_model.noteProbeOutcome(edge.key, delay, verifiedSafe);
        if (edge.coarseKey)
            m_model.noteProbeOutcome(edge.coarseKey, delay, verifiedSafe);
        ++verifyUpdates;
        return;
    }
}
void EbHybridLearning::vanilla(PlayLayer* layer, PlayerObject* player, bool down, bool player1,
                               int tick, float dt) {
    if (!enabled())
        return;
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.key) {
        EbPlanOptions options;
        auto const params = EbStageConfig::paramsFor(player);
        prepare(layer, player, down, player1, tick, params.ticks, dt, dt, options);
    }
    if (!edge.key)
        return;
    // This is observed vanilla survival, with no simulated rescue claim.
    edge.begin(0, -1, -1, false, false);
    edge.horizon = observationTicks;
    m_pending = true;
}
void EbHybridLearning::decision(bool player1, EbSimPlan const& plan) {
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.key)
        return;
    if (plan.hybridTried) {
        ++tried;
        if ((plan.action == PlanAction::Delay && plan.delay == edge.hint) ||
            (edge.hint == 0 && plan.action == PlanAction::Pass && plan.nowIsSafe))
            ++confirmed;
    }
    if (plan.unknown || plan.toggleUnknown || plan.firePhase != 0.f || plan.requireHold)
        return;
    bool const delayed = plan.action == PlanAction::Delay;
    if (!delayed && !plan.nowIsSafe)
        return;
    bool const checkLanding =
        edge.mode == static_cast<int>(GameModeKind::Ball) && !plan.requireOrb &&
        EbAssistPolicySettings::ground() != EbAssistPolicySettings::Policy::Off;
    lastDelay = delayed ? plan.delay : 0;
    m_pending = edge.begin(delayed ? plan.delay : 0, plan.planId, plan.firstDeathTick,
                           plan.requireOrb, checkLanding);
    if (m_pending)
        edge.horizon = std::min(96, std::max(edge.horizon, observationTicks));
}
void EbHybridLearning::fired(bool player1, int planId, int delay, float phase) {
    auto& edge = m_edges[player1 ? 0 : 1];
    edge.fire(planId, delay, phase);
}
void EbHybridLearning::poll(PlayLayer* layer, int tick) {
    if (!m_pending)
        return;
    if (!enabled()) {
        boundary(false);
        return;
    }
    for (int slot = 0; slot < 2; ++slot) {
        auto& edge = m_edges[slot];
        if (edge.delay < 0)
            continue;
        auto* player = slot ? layer->m_player2 : layer->m_player1;
        if (!player || player->m_isDead)
            continue;
        if (!EbSimTrust::get().okAt(player->getPositionX())) {
            ++drift;
            m_previous = {}; // Simulation disagreement cannot grade a physical timing.
            edge = {};
            m_pending = false;
            return;
        }
        if (edge.mode != static_cast<int>(EbStageConfig::kindOf(player))) {
            edge = {};
            m_pending = false;
            return;
        }
        bool const grounded = player->m_isOnGround || player->m_isOnGround2;
        edge.physics(grounded, player->m_touchedRing,
                     static_cast<int>(player->m_touchedRings.size()));
        if (!edge.finished(tick))
            continue;
        int const reward = edge.rewardAt(tick, false);
        if (reward) {
            feedback(edge, reward, -1, 0,
                     edge.predictedDeath && tick - edge.tick > edge.baselineDeathTick);
            ++observations;
        }
        edge = {};
        m_pending = false;
    }
}
void EbHybridLearning::death(bool player1, int tick) {
    auto const& edge = m_edges[player1 ? 0 : 1];
    if (enabled() && trackOutcomes && m_previous.key && tick >= m_previous.tick &&
        tick - m_previous.tick <= m_previous.horizon)
        feedback(m_previous, 0, tick - m_previous.tick, m_intervening);
    int const reward = edge.rewardAt(tick, true);
    if (reward && enabled()) {
        feedback(edge, -deathPenalty, tick - edge.tick);
        ++observations;
        ++deaths;
        lastDeathAge = tick - edge.tick;
        creditEligibility(tick);
    }
    boundary(false);
}
void EbHybridLearning::feedback(EbHybridObservation const& edge, int reward, int deathAge,
                                int intervening, bool survivedBaseline) {
    EbHybridModel::Evidence e;
    e.successCount = reward > 0;
    e.failureCount = deathAge >= 0 && !intervening;
    e.unknownCount = !reward && !e.failureCount;
    e.deathDeltaTicks = deathAge;
    e.interveningInputs = intervening;
    e.timingPhase = edge.phaseBin;
    e.inputSequenceSignature = edge.sequence;
    e.verifiedRescueCount = reward == 3 && survivedBaseline && edge.delay > 0 && edge.delivered;
    int const applied = shapeReward(edge, reward, deathAge, survivedBaseline);
    m_model.observe(edge.key, edge.delay, applied, trackOutcomes ? &e : nullptr);
    if (edge.coarseKey)
        m_model.observe(edge.coarseKey, edge.delay, applied, trackOutcomes ? &e : nullptr);
    if (applied > 0 && edge.delivered)
        pushTrace(edge);
    // Survivals at 1t and 4t should lift 2t/3t so Hybrid can verify the band.
    if (bridgeInterpolate && applied > 0) {
        m_model.bridgeBetweenSurvivors(edge.key, 96, minimumAttempts);
        if (edge.coarseKey)
            m_model.bridgeBetweenSurvivors(edge.coarseKey, 96,
                                           std::max(minimumAttempts, coarseAttempts));
    }
}
void EbHybridLearning::freeze(bool value) {
    boundary(false);
    m_model.setFrozen(value);
}
bool EbHybridLearning::frozen() const {
    return m_model.frozen();
}
std::vector<EbHybridModel::Inspection> EbHybridLearning::inspect() const {
    return m_model.inspect(minimumAttempts);
}
int EbHybridLearning::familiar() const {
    return m_model.familiar();
}
int EbHybridLearning::uniqueKeys() const {
    return m_model.uniqueKeyCount();
}
unsigned EbHybridLearning::totalPulls() const {
    return m_model.totalPulls();
}
