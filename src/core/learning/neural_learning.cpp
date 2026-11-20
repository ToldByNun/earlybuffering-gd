#include "neural_learning.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../level/section_object_walk.hpp"
#include "../platform/platform_limits.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "hybrid_learning.hpp"
#include "neural_bootstrap.hpp"
#include "neural_grid.hpp"
#include "neural_hitbox.hpp"
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
    // Geode packages resource files flat under getResourcesDir(); also try ai/.
    auto const root = mod->getResourcesDir();
    std::filesystem::path const candidates[] = {root / "prior.ebnn", root / "ai" / "prior.ebnn",
                                                root / "resources" / "ai" / "prior.ebnn"};
    for (auto const& prior : candidates) {
        if (!std::filesystem::exists(prior))
            continue;
        if (m_model.load(prior))
            return true;
    }
    return false;
}

bool EbNeuralLearning::loadPrior() {
    if (!tryLoadPrior())
        return false;
    m_hasPriorOrWeights = true;
    m_loaded = true;
    m_model.setFrozen(false);
    // Persist into the per-level path immediately when a level file is open.
    save();
    return true;
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

void EbNeuralLearning::captureGrid(PlayLayer* layer, PlayerObject* player, EbNeuralGrid& outGrid,
                                   float& originX, float& originY, bool storeVizHitboxes) {
    outGrid.clear();
    originX = 0.0f;
    originY = 0.0f;
    if (storeVizHitboxes) {
        m_viz.hitboxCount = 0;
        m_viz.playerHitboxValid = false;
    }
    if (!layer || !player)
        return;
    float const playerX = player->getPositionX();
    float const playerY = player->getPositionY();
    originX = EbNeuralGrid::gridStartX(playerX);
    originY = EbNeuralGrid::gridStartY(playerY);
    float const maxX = originX + EbNeuralGrid::kGridWidth;
    float const maxY = originY + EbNeuralGrid::kGridHeight;
    // Cheap position pad before getObjectRect (avoids hitbox work far off-grid).
    float const pad = 90.0f;

    std::vector<EbNeuralGridObject> objects;
    objects.reserve(96);

    EbSectionObjectWalk::forEachOwnedObject(layer, [&](GameObject* object) {
        if (!object)
            return;
        std::uint8_t const bits = channelBitsForObject(object);
        if (!bits)
            return;
        float const ox = object->getPositionX();
        float const oy = object->getPositionY();
        if (ox < originX - pad || ox > maxX + pad || oy < originY - pad || oy > maxY + pad)
            return;

        EbNeuralHitbox const hit = EbNeuralHitbox::read(object);
        if (!hit.valid)
            return;
        if (hit.aabbX + hit.aabbW < originX || hit.aabbX > maxX ||
            hit.aabbY + hit.aabbH < originY || hit.aabbY > maxY)
            return;

        EbNeuralGridObject entry;
        entry.x = hit.aabbX;
        entry.y = hit.aabbY;
        entry.width = hit.aabbW;
        entry.height = hit.aabbH;
        entry.channelBits = bits;
        entry.oriented = hit.oriented;
        for (int corner = 0; corner < 4; ++corner) {
            entry.cornerX[corner] = hit.cornerX[corner];
            entry.cornerY[corner] = hit.cornerY[corner];
        }
        objects.push_back(entry);

        if (storeVizHitboxes && m_viz.hitboxCount < EbNeuralVizSnapshot::kMaxHitboxes) {
            auto& draw = m_viz.hitboxes[m_viz.hitboxCount++];
            draw.channelBits = bits;
            draw.oriented = hit.oriented;
            draw.aabbX = hit.aabbX;
            draw.aabbY = hit.aabbY;
            draw.aabbW = hit.aabbW;
            draw.aabbH = hit.aabbH;
            for (int corner = 0; corner < 4; ++corner) {
                draw.cornerX[corner] = hit.cornerX[corner];
                draw.cornerY[corner] = hit.cornerY[corner];
            }
        }
    });
    outGrid.rasterize(playerX, playerY, objects.data(), static_cast<int>(objects.size()));

    if (storeVizHitboxes) {
        EbNeuralHitbox const playerHit = EbNeuralHitbox::readPlayer(player);
        if (playerHit.valid) {
            m_viz.playerHitboxValid = true;
            m_viz.playerAabbX = playerHit.aabbX;
            m_viz.playerAabbY = playerHit.aabbY;
            m_viz.playerAabbW = playerHit.aabbW;
            m_viz.playerAabbH = playerHit.aabbH;
        }
    }
}

void EbNeuralLearning::publishVizRanks() {
    m_viz.rankedCount = 0;
    m_viz.hintDelay = -1;
    for (auto const& edge : m_edges) {
        if (!edge.valid || edge.verifyOrder.empty())
            continue;
        m_viz.hintDelay = edge.hint;
        int const count =
            std::min(EbNeuralVizSnapshot::kMaxRanked, static_cast<int>(edge.verifyOrder.size()));
        for (int index = 0; index < count; ++index) {
            int const delay = edge.verifyOrder[static_cast<size_t>(index)];
            m_viz.rankedDelays[index] = delay;
            float scratch[EbNeuralModel::kFeatureDim];
            std::memcpy(scratch, edge.features, sizeof(scratch));
            EbNeuralModel::writeCandidateDelay(scratch, delay);
            m_viz.rankedConfidence[index] = m_model.confidence(scratch);
        }
        m_viz.rankedCount = count;
        break;
    }
    m_viz.lastArmedDelay = lastArmedDelay;
    m_viz.lastArmedConfidence = lastArmedConfidence;
    m_viz.updates = updates;
    m_viz.inferences = inferences;
    m_viz.verifies = verifies;
    m_viz.verifyHits = verifyHits;
    m_viz.training = training();
    m_viz.hasWeights = m_hasPriorOrWeights;
}

void EbNeuralLearning::refreshViz(PlayLayer* layer) {
    if (!layer || !layer->m_player1)
        return;
    PlayerObject* player = layer->m_player1;
    EbNeuralGrid grid;
    float originX = 0.0f;
    float originY = 0.0f;
    captureGrid(layer, player, grid, originX, originY, true);
    m_viz.originX = originX;
    m_viz.originY = originY;
    m_viz.playerX = player->getPositionX();
    m_viz.playerY = player->getPositionY();
    m_viz.playerSpeed = std::max(0.2f, player->m_playerSpeed);
    m_viz.goingLeft = player->m_isGoingLeft;
    m_viz.sideways = player->m_isSideways;
    grid.copyCells(m_viz.cells);
    publishVizRanks();
    m_viz.valid = true;
}

EbNeuralVizSnapshot const& EbNeuralLearning::viz() const {
    return m_viz;
}

void EbNeuralLearning::fillFeatures(PlayLayer* layer, PlayerObject* player, bool down, bool player1,
                                    int limit, float dt, float firstDt, float* outFeatures) {
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
    EbNeuralGrid grid;
    float originX = 0.0f;
    float originY = 0.0f;
    captureGrid(layer, player, grid, originX, originY, false);

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

    m_viz.originX = originX;
    m_viz.originY = originY;
    m_viz.playerX = playerX;
    m_viz.playerY = playerY;
    m_viz.playerSpeed = std::max(0.2f, player->m_playerSpeed);
    m_viz.goingLeft = player->m_isGoingLeft;
    m_viz.sideways = player->m_isSideways;
    grid.copyCells(m_viz.cells);
    m_viz.valid = true;
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

    int const k = std::clamp(topK, 1, 8);
    auto neuralRanked = m_model.rankedDelays(edge.features, edge.limit, k);
    // AP06: Hybrid tables may propose when present (planner already called Hybrid prepare).
    std::vector<int> hybridRanked;
    if (EbHybridLearning::tablesReadable())
        hybridRanked = EbHybridLearning::get().rankedVerifyDelays(edge.limit);
    edge.verifyOrder = EbNeuralBootstrap::mergeVerifyOrder(hybridRanked, neuralRanked, k);
    // Soft distill Hybrid mature delays into the scorer while training (transfer).
    if (training() && !hybridRanked.empty()) {
        int const distill = std::min(2, static_cast<int>(hybridRanked.size()));
        for (int index = 0; index < distill; ++index)
            m_model.learnRank(edge.features, hybridRanked[static_cast<size_t>(index)], edge.limit,
                              learningRate * 0.25f);
        m_hasPriorOrWeights = true;
    }
    ++inferences;
    publishVizRanks();
    if (edge.verifyOrder.empty())
        return -1;
    edge.hint = edge.verifyOrder.front();
    return edge.hint;
}

std::vector<int> EbNeuralLearning::rankedVerifyDelays(int limit) const {
    if (!active() || !m_loaded)
        return {};
    for (auto const& edge : m_edges) {
        if (!edge.valid)
            continue;
        if (!edge.verifyOrder.empty())
            return edge.verifyOrder;
        int const k = std::clamp(topK, 1, 8);
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
    expect(get().topK >= 1 && get().topK <= 8, "topk-range");
    expect(EbNeuralModel::kFeatureDim == 3512, "feature-dim");
    expect(channelBitsForObject(nullptr) == 0, "null-object");
    std::string hitboxReport;
    expect(EbNeuralHitbox::selfCheck(hitboxReport), hitboxReport.c_str());
    std::string gridReport;
    expect(EbNeuralGrid::selfCheck(gridReport), gridReport.c_str());
    std::string bootstrapReport;
    expect(EbNeuralBootstrap::selfCheck(bootstrapReport), bootstrapReport.c_str());

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
