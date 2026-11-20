#include "neural_learning.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../../level/section_object_walk.hpp"
#include "../../platform/platform_limits.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "neural_grid.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

EbNeuralLearning& EbNeuralLearning::get() {
    static EbNeuralLearning instance;
    return instance;
}

bool EbNeuralLearning::active() {
    return EbStageConfig::assistMode() == EbStageConfig::AssistMode::AI;
}

bool EbNeuralLearning::training() {
    return active() && EbStageConfig::autoTuneEnabled() && !get().frozen();
}

void EbNeuralLearning::clearEdges() {
    m_edges = {};
}

void EbNeuralLearning::open(std::filesystem::path path) {
    close();
    m_path = std::move(path);
    settingsChanged();
}

void EbNeuralLearning::settingsChanged() {
    boundary(false);
    if (m_path.empty() || m_loaded || !active())
        return;
    m_hasPriorOrWeights = m_model.load(m_path);
    if (!m_hasPriorOrWeights)
        m_hasPriorOrWeights = tryLoadPrior();
    if (!m_hasPriorOrWeights)
        m_model.reset();
    m_loaded = true;
}

bool EbNeuralLearning::tryLoadPrior() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return false;
    auto prior = mod->getResourcesDir() / "ai" / "prior.ebnn";
    if (!std::filesystem::exists(prior))
        return false;
    return m_model.load(prior);
}

void EbNeuralLearning::close() {
    save();
    m_path.clear();
    m_loaded = false;
    m_hasPriorOrWeights = false;
    m_model.reset();
    m_replay.clear();
    edges = inferences = verifies = verifyHits = updates = deaths = tried = confirmed = 0;
    lastArmedDelay = -1;
    lastArmedConfidence = 0.0f;
    clearEdges();
}

void EbNeuralLearning::reset() {
    if (m_path.empty())
        return;
    m_loaded = true;
    m_model.reset();
    m_hasPriorOrWeights = false;
    m_replay.clear();
    edges = inferences = verifies = verifyHits = updates = deaths = tried = confirmed = 0;
    lastArmedDelay = -1;
    lastArmedConfidence = 0.0f;
    clearEdges();
    save();
}

void EbNeuralLearning::save() {
    if (!m_loaded || m_path.empty())
        return;
    if (!m_model.save(m_path))
        EbAssistLog::info("AI: could not save level neural weights");
}

void EbNeuralLearning::boundary(bool newAttempt) {
    clearEdges();
    (void)newAttempt;
}

void EbNeuralLearning::freeze(bool value) {
    m_model.setFrozen(value);
}

bool EbNeuralLearning::frozen() const {
    return m_model.frozen();
}

bool EbNeuralLearning::loaded() const {
    return m_loaded;
}

bool EbNeuralLearning::hasUsableWeights() const {
    return m_loaded && m_hasPriorOrWeights;
}

EbNeuralModel& EbNeuralLearning::model() {
    return m_model;
}

EbNeuralModel const& EbNeuralLearning::model() const {
    return m_model;
}

EbNeuralReplay& EbNeuralLearning::replay() {
    return m_replay;
}

std::uint8_t EbNeuralLearning::channelBitsForObject(GameObject* object) {
    if (!object)
        return 0;
    auto const type = static_cast<GameObjectType>(object->m_objectType);
    switch (type) {
    case GameObjectType::Solid:
    case GameObjectType::Slope:
    case GameObjectType::Breakable:
        return EbNeuralGrid::kChannelSolid;
    case GameObjectType::Hazard:
    case GameObjectType::AnimatedHazard:
        return EbNeuralGrid::kChannelHazard;
    case GameObjectType::YellowJumpPad:
    case GameObjectType::PinkJumpPad:
    case GameObjectType::RedJumpPad:
    case GameObjectType::GravityPad:
    case GameObjectType::SpiderPad:
    case GameObjectType::YellowJumpRing:
    case GameObjectType::PinkJumpRing:
    case GameObjectType::RedJumpRing:
    case GameObjectType::GravityRing:
    case GameObjectType::GreenRing:
    case GameObjectType::DropRing:
    case GameObjectType::DashRing:
    case GameObjectType::GravityDashRing:
    case GameObjectType::SpiderOrb:
    case GameObjectType::CustomRing:
        return EbNeuralGrid::kChannelOrbPad;
    default:
        return 0;
    }
}

void EbNeuralLearning::fillFeatures(PlayLayer* layer, PlayerObject* player, bool down, bool player1,
                                    int limit, float dt, float firstDt, float* outFeatures) const {
    if (!outFeatures || !layer || !player)
        return;
    auto clampGeom = [](float value) {
        if (value < -3.0f)
            return -3.0f;
        if (value > 3.0f)
            return 3.0f;
        return value;
    };
    float const playerX = player->getPositionX();
    float const playerY = player->getPositionY();
    float const originX = EbNeuralGrid::gridStartX(playerX);
    float const originY = EbNeuralGrid::gridStartY(playerY);
    float const maxX = originX + EbNeuralGrid::kGridWidth;
    float const maxY = originY + EbNeuralGrid::kGridHeight;

    std::vector<EbNeuralGridObject> objects;
    objects.reserve(128);
    EbSectionObjectWalk::forEachOwnedObject(layer, [&](GameObject* object) {
        if (!object)
            return;
        std::uint8_t const bits = channelBitsForObject(object);
        if (!bits)
            return;
        float const width = object->getContentSize().width * std::fabs(object->getScaleX());
        float const height = object->getContentSize().height * std::fabs(object->getScaleY());
        float const x = object->getPositionX() - width * 0.5f;
        float const y = object->getPositionY() - height * 0.5f;
        if (x + width < originX || x > maxX || y + height < originY || y > maxY)
            return;
        EbNeuralGridObject entry;
        entry.x = x;
        entry.y = y;
        entry.width = std::max(1.0f, width);
        entry.height = std::max(1.0f, height);
        entry.channelBits = bits;
        objects.push_back(entry);
    });

    EbNeuralGrid grid;
    grid.rasterize(playerX, playerY, objects.data(), static_cast<int>(objects.size()));

    EbNeuralFeatureState state;
    state.modeKind = static_cast<int>(EbStageConfig::kindOf(player));
    state.press = down;
    state.player1 = player1;
    state.dual = layer->m_gameState.m_isDualMode;
    state.practice = layer->m_isPracticeMode;
    state.grounded = player->m_isOnGround || player->m_isOnGround2;
    state.xFrac = std::fmod(std::fabs(playerX), 64.0f) / 64.0f;
    state.yNorm = clampGeom(playerY / 240.0f);
    state.vxSign = player->m_isGoingLeft ? -1.0f : 1.0f;
    state.vyNorm = clampGeom(static_cast<float>(player->m_yVelocity) / 20.0f);
    state.speedNorm = player->m_playerSpeed;
    state.sizeNorm = player->m_vehicleSize;
    state.gravityMod = player->m_gravityMod;
    state.upsideDown = player->m_isUpsideDown;
    state.goingLeft = player->m_isGoingLeft;
    state.sideways = player->m_isSideways;
    state.limit = limit;
    state.limitNorm = static_cast<float>(EbNeuralModel::clampLimit(limit)) /
                      static_cast<float>(EbNeuralModel::kMaxDelay);
    state.remainingBudgetNorm = 1.0f;
    state.firstDeathHintNorm = -1.0f;
    state.nearOrb = player->m_touchedRing || !player->m_touchedRings.empty();
    state.candidateDelay = 0;
    if (dt > 0.0f)
        state.geometry[0] = clampGeom((1.0f / dt) / 240.0f);
    state.geometry[1] = firstDt < dt * 0.99f ? 1.0f : 0.0f;
    state.geometry[2] = clampGeom(layer->m_gameState.m_timeWarp);

    EbNeuralFeatures::build(outFeatures, grid, state);
}

void EbNeuralLearning::edge(bool player1, int tick, bool down) {
    if (!active())
        return;
    ++edges;
    auto& edge = m_edges[player1 ? 0 : 1];
    edge.tick = tick;
    edge.down = down;
    edge.player1 = player1;
}

int EbNeuralLearning::prepare(PlayLayer* layer, PlayerObject* player, bool down, bool player1,
                              int tick, int limit, float dt, float firstDt,
                              EbPlanOptions const& options) {
    (void)options;
    if (!active() || !m_loaded || !layer || !player || dt <= 0.0f)
        return -1;
    // Inference needs prior/weights unless we are actively training a fresh net.
    if (!m_hasPriorOrWeights && !training())
        return -1;
    if (layer->m_gameState.m_isDualMode)
        return -1;

    auto& edge = m_edges[player1 ? 0 : 1];
    fillFeatures(layer, player, down, player1, limit, dt, firstDt, edge.features);
    edge.valid = true;
    edge.down = down;
    edge.player1 = player1;
    edge.tick = tick;
    edge.limit = EbNeuralModel::clampLimit(limit);
    edge.hint = -1;
    edge.plannedDelay = -1;
    edge.planId = -1;
    edge.fired = false;
    edge.pending = false;

    int const k = std::clamp(topK, 1, 3);
    auto ranked = m_model.rankedDelays(edge.features, edge.limit, k);
    ++inferences;
    if (ranked.empty())
        return -1;
    edge.hint = ranked.front();
    return edge.hint;
}

std::vector<int> EbNeuralLearning::rankedVerifyDelays(int limit) const {
    if (!active() || !m_loaded)
        return {};
    int const k = std::clamp(topK, 1, 3);
    for (auto const& edge : m_edges) {
        if (!edge.valid)
            continue;
        int const clamped = EbNeuralModel::clampLimit(limit > 0 ? limit : edge.limit);
        return m_model.rankedDelays(edge.features, clamped, k);
    }
    return {};
}

float EbNeuralLearning::confidenceForDelay(int delay) const {
    if (!active() || !m_loaded || delay < 0)
        return 0.0f;
    for (auto const& edge : m_edges) {
        if (!edge.valid)
            continue;
        float scratch[EbNeuralModel::kFeatureDim];
        std::memcpy(scratch, edge.features, sizeof(scratch));
        EbNeuralModel::writeCandidateDelay(scratch, delay);
        return m_model.confidence(scratch);
    }
    return 0.0f;
}

int EbNeuralLearning::minRemUsForAiFirst() {
    return EbPlatformLimits::isConstrainedDevice() ? 250 : 400;
}

void EbNeuralLearning::noteArmed(int delay, float confidence) {
    lastArmedDelay = delay;
    lastArmedConfidence = confidence;
}

void EbNeuralLearning::noteVerifyOutcome(int delay, bool verifiedSafe) {
    if (!active() || !m_loaded || delay < 0)
        return;
    ++verifies;
    if (verifiedSafe)
        ++verifyHits;
    if (!training())
        return;
    for (auto& edge : m_edges) {
        if (!edge.valid)
            continue;
        float scratch[EbNeuralModel::kFeatureDim];
        std::memcpy(scratch, edge.features, sizeof(scratch));
        EbNeuralModel::writeCandidateDelay(scratch, delay);
        m_model.learnConfidence(scratch, verifiedSafe ? 1.0f : 0.0f, learningRate);
        if (verifiedSafe)
            m_model.learnRank(edge.features, delay, edge.limit, learningRate);
        else
            m_model.learnTd(edge.features, delay, -1.0f, edge.features, true, edge.limit,
                            learningRate);
        ++updates;
        m_hasPriorOrWeights = true;
        return;
    }
}

void EbNeuralLearning::vanilla(PlayLayer* layer, PlayerObject* player, bool down, bool player1,
                               int tick, float dt) {
    if (!active() || !m_loaded)
        return;
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.valid) {
        EbPlanOptions options;
        auto const params = EbStageConfig::paramsFor(player);
        prepare(layer, player, down, player1, tick, params.ticks, dt, dt, options);
    }
    if (!edge.valid)
        return;
    edge.plannedDelay = 0;
    edge.pending = true;
}

void EbNeuralLearning::decision(bool player1, EbSimPlan const& plan) {
    if (!active() || !m_loaded)
        return;
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.valid)
        return;
    if (plan.aiTried) {
        ++tried;
        if ((plan.action == PlanAction::Delay && plan.delay == edge.hint) ||
            (edge.hint == 0 && plan.action == PlanAction::Pass && plan.nowIsSafe))
            ++confirmed;
    }
    bool const delayed = plan.action == PlanAction::Delay;
    edge.plannedDelay = delayed ? plan.delay : 0;
    edge.planId = plan.planId;
    edge.pending = true;
    edge.fired = false;
}

void EbNeuralLearning::fired(bool player1, int planId, int delay, float phase) {
    (void)phase;
    if (!active() || !m_loaded)
        return;
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.valid)
        return;
    if (planId >= 0 && edge.planId >= 0 && planId != edge.planId)
        return;
    edge.fired = true;
    edge.plannedDelay = delay;
}

void EbNeuralLearning::poll(PlayLayer* layer, int tick) {
    (void)layer;
    if (!active() || !m_loaded)
        return;
    for (auto& edge : m_edges) {
        if (!edge.valid || !edge.pending || !edge.fired)
            continue;
        if (tick - edge.tick < observationTicks)
            continue;
        if (training() && edge.plannedDelay >= 0) {
            float next[EbNeuralModel::kFeatureDim];
            std::memcpy(next, edge.features, sizeof(next));
            m_model.learnTd(edge.features, edge.plannedDelay, 1.0f, next, false, edge.limit,
                            learningRate);
            m_model.learnRank(edge.features, edge.plannedDelay, edge.limit, learningRate);
            m_replay.push(edge.features, edge.plannedDelay, 1.0f, next, false);
            ++updates;
            m_hasPriorOrWeights = true;
        }
        edge.pending = false;
    }
}

void EbNeuralLearning::death(bool player1, int tick) {
    if (!active() || !m_loaded)
        return;
    ++deaths;
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.valid || !edge.pending)
        return;
    if (training() && edge.plannedDelay >= 0) {
        float next[EbNeuralModel::kFeatureDim];
        std::memcpy(next, edge.features, sizeof(next));
        m_model.learnTd(edge.features, edge.plannedDelay, -2.0f, next, true, edge.limit,
                        learningRate);
        m_replay.push(edge.features, edge.plannedDelay, -2.0f, next, true);
        ++updates;
        m_hasPriorOrWeights = true;
    }
    edge.pending = false;
    (void)tick;
}

bool EbNeuralLearning::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    expect(get().topK >= 1 && get().topK <= 3, "topk-range");
    expect(EbNeuralModel::kFeatureDim == 3512, "feature-dim");
    expect(channelBitsForObject(nullptr) == 0, "null-object");

    EbNeuralModel model;
    model.reset();
    model.setFrozen(true);
    float features[EbNeuralModel::kFeatureDim]{};
    features[EbNeuralModel::kContinuousOffset] = 0.2f;
    float const before = model.confidence(features);
    model.learnConfidence(features, 1.0f, 0.05f);
    expect(model.confidence(features) == before, "frozen-blocks-learn");
    model.setFrozen(false);

    if (fails == 0) {
        report = "NeuralLearning OK";
        return true;
    }
    report = std::string("NeuralLearning fails: ") + report;
    return false;
}
