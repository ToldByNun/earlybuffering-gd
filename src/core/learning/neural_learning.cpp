#include "neural_learning.hpp"
#include "../../config/stage_config.hpp"
#include "../../debug/assist_log.hpp"
#include "../level/section_object_walk.hpp"
#include "../platform/platform_limits.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/shadow_simulator.hpp"
#include "ai_config.hpp"
#include "ai_route_planner.hpp"
#include "hybrid_learning.hpp"
#include "level_profile.hpp"
#include "neural_autoplay.hpp"
#include "neural_bootstrap.hpp"
#include "neural_grid.hpp"
#include "neural_hitbox.hpp"
#include "neural_session.hpp"
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

void EbNeuralLearning::softResetAttemptState() {
    EbNeuralSession::get().clear();
    m_sessionTrainedThrough = 0;
    m_recent = {};
    m_recentNext = 0;
    clearEdges();
}

std::filesystem::path EbNeuralLearning::userPriorPath() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return {};
    return mod->getSaveDir() / "ai" / "prior.ebnn";
}

void EbNeuralLearning::open(std::filesystem::path path) {
    if (m_trainBasePrior) {
        (void)path;
        openSharedBase();
        return;
    }
    close();
    m_path = std::move(path);
    softResetAttemptState();
    settingsChanged();
}

void EbNeuralLearning::openSharedBase() {
    auto const base = userPriorPath();
    if (base.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(base.parent_path(), ec);
    if (m_loaded && m_path == base) {
        // Level hop while training the shared base: keep weights, refresh session only.
        save();
        softResetAttemptState();
        EbAssistLog::info("AI: base prior — new level, weights kept");
        return;
    }
    if (m_loaded)
        save();
    m_path = base;
    if (!m_loaded) {
        m_hasPriorOrWeights = m_model.load(m_path);
        if (!m_hasPriorOrWeights)
            m_hasPriorOrWeights = tryLoadPackagedPrior();
        if (!m_hasPriorOrWeights)
            m_model.reset();
        m_loaded = true;
    } else {
        m_hasPriorOrWeights = true;
    }
    softResetAttemptState();
    EbAssistLog::info("AI: training shared base prior (cross-level)");
}

void EbNeuralLearning::setTrainBasePrior(bool on) {
    if (on == m_trainBasePrior)
        return;
    if (m_loaded)
        save();
    m_trainBasePrior = on;
    if (!m_loaded)
        return;
    if (on) {
        openSharedBase();
        save();
        return;
    }
    // Leave base mode: seed this level's .neural from the shared weights.
    auto& profile = EbLevelProfile::get();
    if (profile.loaded() && !profile.key().empty()) {
        m_path = geode::prelude::Mod::get()->getSaveDir() / "levels" / (profile.key() + ".neural");
        save();
        EbAssistLog::info("AI: per-level fine-tune — seeded from base prior");
    }
}

void EbNeuralLearning::settingsChanged() {
    boundary(false);
    auto& cfg = EbAiConfig::get();
    cfg.load();
    preferAiFirst = cfg.preferAiFirst;
    topK = std::clamp(cfg.maxCandidates, 1, 8);
    learningRate = cfg.learningRate;
    if (cfg.freezeWeights)
        freeze(true);
    EbNeuralAutoplay::get().syncFromConfig();
    if (m_path.empty() || m_loaded || !active())
        return;
    m_hasPriorOrWeights = m_model.load(m_path);
    if (!m_hasPriorOrWeights)
        m_hasPriorOrWeights = tryLoadPrior();
    if (!m_hasPriorOrWeights)
        m_model.reset();
    m_loaded = true;
}

bool EbNeuralLearning::tryLoadPackagedPrior() {
    auto* mod = geode::prelude::Mod::get();
    if (!mod)
        return false;
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

bool EbNeuralLearning::tryLoadPrior() {
    auto const user = userPriorPath();
    if (!user.empty() && std::filesystem::exists(user) && m_model.load(user))
        return true;
    return tryLoadPackagedPrior();
}

bool EbNeuralLearning::loadPrior() {
    if (!tryLoadPrior())
        return false;
    m_hasPriorOrWeights = true;
    m_loaded = true;
    m_model.setFrozen(false);
    // Persist into the active path (level .neural or shared base).
    save();
    return true;
}

bool EbNeuralLearning::saveAsBasePrior() {
    if (!m_loaded)
        return false;
    auto const base = userPriorPath();
    if (base.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(base.parent_path(), ec);
    if (!m_model.save(base)) {
        EbAssistLog::info("AI: could not save base prior");
        return false;
    }
    EbAssistLog::info(fmt::format("AI: saved base prior → {}", base.string()));
    return true;
}

void EbNeuralLearning::close() {
    save();
    m_path.clear();
    m_loaded = false;
    m_hasPriorOrWeights = false;
    m_model.reset();
    m_replay.clear();
    EbNeuralSession::get().clear();
    EbNeuralAutoplay::get().setEnabled(false);
    EbNeuralAutoplay::get().syncTimeWarp(PlayLayer::get());
    m_recent = {};
    m_recentNext = 0;
    m_sessionTrainedThrough = 0;
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
    EbNeuralSession::get().clear();
    m_recent = {};
    m_recentNext = 0;
    m_sessionTrainedThrough = 0;
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
        EbAssistLog::info(m_trainBasePrior ? "AI: could not save base prior"
                                           : "AI: could not save level neural weights");
}

int EbNeuralLearning::trainSession(int passes, bool persist) {
    if (!m_loaded)
        return 0;
    int const trained = EbNeuralSession::get().train(m_model, learningRate, passes);
    if (trained > 0) {
        this->updates += trained;
        m_hasPriorOrWeights = true;
        m_sessionTrainedThrough = EbNeuralSession::get().size();
        if (persist)
            save();
    }
    return trained;
}

int EbNeuralLearning::trainSessionIfGrown(int passes, int maxUpdates) {
    if (!m_loaded || !training())
        return 0;
    int const size = EbNeuralSession::get().size();
    // Session clear / shrink → allow future auto-trains from an empty baseline.
    if (size < m_sessionTrainedThrough)
        m_sessionTrainedThrough = size;
    if (size <= m_sessionTrainedThrough || passes <= 0)
        return 0;
    int const start = m_sessionTrainedThrough;
    // passes=1 in the deferred path so return count == samples consumed.
    int const trained =
        EbNeuralSession::get().train(m_model, learningRate, std::max(1, passes), start, size,
                                     maxUpdates);
    if (trained > 0) {
        this->updates += trained;
        m_hasPriorOrWeights = true;
        m_sessionTrainedThrough = std::min(size, start + trained);
        // Disk save is hitchy — only every few deferred flushes (or manual Train Session).
        if (++m_savesSincePersist >= 8) {
            m_savesSincePersist = 0;
            save();
        }
    }
    return trained;
}

void EbNeuralLearning::flushDeferredTrain() {
    if (!m_deferTrain)
        return;
    m_deferTrain = false;
    trainSessionIfGrown(1, 48);
}

void EbNeuralLearning::boundary(bool newAttempt) {
    clearEdges();
    if (newAttempt) {
        // Heavy neural batch belongs here (next attempt), never on destroyPlayer.
        flushDeferredTrain();
        EbNeuralSession::get().endAttempt();
        EbNeuralSession::get().beginAttempt();
    }
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

void EbNeuralLearning::fillTrajectoryPair(PlayLayer* layer, PlayerObject* player, bool down,
                                          int limit, float dt, float firstDt,
                                          EbNeuralFeatureState& state) {
    if (!layer || !player || dt <= 0.0f)
        return;
    auto const pair =
        EbShadowSimulator::get().sampleActionVsIdle(layer, player, down, limit, dt, firstDt);
    if (!pair.valid || pair.horizon <= 0)
        return;

    int const horizon = pair.horizon;
    auto surviveNorm = [horizon](bool died, int deathTick) -> float {
        if (!died)
            return 1.0f;
        if (deathTick < 0)
            return 0.0f;
        return std::clamp(static_cast<float>(deathTick + 1) / static_cast<float>(horizon), 0.0f,
                          1.0f);
    };
    float const idleSurvive = surviveNorm(pair.idleDied, pair.idleDeathTick);
    float const actionSurvive = surviveNorm(pair.actionDied, pair.actionDeathTick);
    float const endXDelta =
        std::clamp((pair.actionEndX - pair.idleEndX) / 120.0f, -3.0f, 3.0f);

    bool const continuous = player->m_isDart || player->m_isShip || player->m_isSwing ||
                            player->m_isBird;
    bool const idleSafer = EbShadowSimulator::classifyIdleSafer(
        continuous, down, pair.idleDied, pair.idleDeathTick, pair.idleDeathX, pair.actionDied,
        pair.actionDeathTick, pair.actionDeathX);

    // +1 = action-now looks better / has a chance; −1 = idle safer (helpless click);
    //  0 = tied / unknown.
    float actionHelps = 0.0f;
    if (pair.unknown) {
        actionHelps = 0.0f;
    } else if (!pair.actionDied && pair.idleDied) {
        actionHelps = 1.0f;
    } else if (pair.actionDied && !pair.idleDied) {
        actionHelps = -1.0f;
    } else if (!pair.actionDied && !pair.idleDied) {
        if (endXDelta > 0.05f)
            actionHelps = 0.5f;
        else if (endXDelta < -0.05f)
            actionHelps = -0.5f;
    } else {
        // Both die inside the horizon — later / further death = at least a chance.
        if (pair.actionDeathTick > pair.idleDeathTick + 1)
            actionHelps = 0.75f;
        else if (pair.idleDeathTick > pair.actionDeathTick + 1)
            actionHelps = -0.75f;
        else if (pair.actionDeathX > pair.idleDeathX + 8.0f)
            actionHelps = 0.5f;
        else if (pair.idleDeathX > pair.actionDeathX + 8.0f)
            actionHelps = -0.5f;
        else
            actionHelps = -1.0f; // same fate → click is helpless noise
    }
    if (idleSafer)
        actionHelps = -1.0f;

    state.geometry[EbNeuralFeatureState::kGeomIdleDied] = pair.idleDied ? 1.0f : 0.0f;
    state.geometry[EbNeuralFeatureState::kGeomActionDied] = pair.actionDied ? 1.0f : 0.0f;
    state.geometry[EbNeuralFeatureState::kGeomIdleSurvive] = idleSurvive;
    state.geometry[EbNeuralFeatureState::kGeomActionSurvive] = actionSurvive;
    state.geometry[EbNeuralFeatureState::kGeomEndXDelta] = endXDelta;
    state.geometry[EbNeuralFeatureState::kGeomActionHelps] = actionHelps;
    state.geometry[EbNeuralFeatureState::kGeomBothDead] =
        (pair.idleDied && pair.actionDied) ? 1.0f : 0.0f;
    state.geometry[EbNeuralFeatureState::kGeomIdleSafer] = idleSafer ? 1.0f : 0.0f;

    int earliest = -1;
    if (pair.idleDied && pair.idleDeathTick >= 0)
        earliest = pair.idleDeathTick;
    if (pair.actionDied && pair.actionDeathTick >= 0) {
        if (earliest < 0 || pair.actionDeathTick < earliest)
            earliest = pair.actionDeathTick;
    }
    state.firstDeathHintNorm =
        earliest < 0 ? -1.0f
                     : std::clamp(static_cast<float>(earliest + 1) / static_cast<float>(horizon),
                                  0.0f, 3.0f);
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
        state.geometry[EbNeuralFeatureState::kGeomFps] = clampGeom((1.0f / dt) / 240.0f);
    state.geometry[EbNeuralFeatureState::kGeomFirstDt] = firstDt < dt * 0.99f ? 1.0f : 0.0f;
    state.geometry[EbNeuralFeatureState::kGeomTimeWarp] =
        clampGeom(layer->m_gameState.m_timeWarp);
    fillTrajectoryPair(layer, player, down, limit, dt, firstDt, state);

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
    edge.startX = player->getPositionX();
    edge.hint = -1;
    edge.plannedDelay = -1;
    edge.planId = -1;
    edge.fired = false;
    edge.pending = false;

    int const k = std::clamp(topK, 1, 8);
    auto const& cfg = EbAiConfig::get();
    // Primary backend: Nova-style per-candidate sigmoid. DualHead is experimental only.
    bool const novaLog1p = cfg.delayEncoding == EbAiConfig::DelayEncoding::Log1p;
    auto neuralRanked =
        cfg.backend == EbAiConfig::Backend::DualHeadLegacy && cfg.experimentalBackends
            ? m_model.rankedDelays(edge.features, edge.limit, k)
            : m_model.rankedDelaysByConfidence(edge.features, edge.limit, k, novaLog1p);
    // AP06: Hybrid tables may propose when present (planner already called Hybrid prepare).
    std::vector<int> hybridRanked;
    if (EbHybridLearning::tablesReadable())
        hybridRanked = EbHybridLearning::get().rankedVerifyDelays(edge.limit);
    edge.verifyOrder = EbNeuralBootstrap::mergeVerifyOrder(hybridRanked, neuralRanked, k);
    // Soft distill Hybrid mature delays into the scorer while training (transfer).
    if (training() && !hybridRanked.empty()) {
        int const distill = std::min(2, static_cast<int>(hybridRanked.size()));
        float const scale = evidenceLearnScale(Evidence::TeacherSuggested);
        for (int index = 0; index < distill; ++index) {
            int const delay = hybridRanked[static_cast<size_t>(index)];
            m_model.learnRank(edge.features, delay, edge.limit, learningRate * scale);
            float scratch[EbNeuralModel::kFeatureDim];
            std::memcpy(scratch, edge.features, sizeof(scratch));
            EbNeuralModel::writeCandidateDelay(scratch, delay);
            m_model.learnConfidence(scratch,
                                    evidenceConfidenceTarget(Evidence::TeacherSuggested, delay, 0.5f),
                                    learningRate * scale);
        }
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
        return m_model.rankedDelaysByConfidence(edge.features, clamped, k);
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
        bool const nova =
            EbAiConfig::get().delayEncoding == EbAiConfig::DelayEncoding::Log1p;
        EbNeuralModel::writeCandidateForPolicy(scratch, delay, edge.down, nova);
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
    // Press hot path: keep rank + confidence heads in sync. No TD on every probe.
    float const scale = evidenceLearnScale(Evidence::SimulationVerified);
    for (auto& edge : m_edges) {
        if (!edge.valid)
            continue;
        float scratch[EbNeuralModel::kFeatureDim];
        std::memcpy(scratch, edge.features, sizeof(scratch));
        bool const nova =
            EbAiConfig::get().delayEncoding == EbAiConfig::DelayEncoding::Log1p;
        EbNeuralModel::writeCandidateForPolicy(scratch, delay, edge.down, nova);
        if (verifiedSafe) {
            m_model.learnConfidence(scratch, 1.0f, learningRate * scale);
            m_model.learnRank(edge.features, delay, edge.limit, learningRate * scale);
            // Supervised-only buffer sample (identical next → session skips TD).
            // Keep reward ≥ 0.5 so session train still updates the rank head.
            float const reward =
                0.85f * evidenceRewardScale(Evidence::SimulationVerified);
            EbNeuralSession::get().push(edge.features, delay, edge.limit, reward, edge.features,
                                        false);
        } else {
            m_model.learnConfidence(scratch, 0.0f, learningRate * scale);
            m_model.learnRankReject(edge.features, delay, edge.limit, learningRate * scale);
        }
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
    // Live fallthrough Delay = weak teacher label (not survived / not verified).
    if (training() && !plan.aiTried && delayed && plan.delay > 0) {
        Evidence const evidence = Evidence::TeacherSuggested;
        float const scale = evidenceLearnScale(evidence);
        m_model.learnRank(edge.features, plan.delay, edge.limit, learningRate * scale);
        float scratch[EbNeuralModel::kFeatureDim];
        std::memcpy(scratch, edge.features, sizeof(scratch));
        EbNeuralModel::writeCandidateDelay(scratch, plan.delay);
        m_model.learnConfidence(
            scratch, evidenceConfidenceTarget(evidence, plan.delay, 0.5f), learningRate * scale);
        float const reward =
            successReward(plan.delay, 0.5f) * evidenceRewardScale(evidence);
        EbNeuralSession::get().push(edge.features, plan.delay, edge.limit, reward, edge.features,
                                    false);
        ++updates;
        m_hasPriorOrWeights = true;
    }
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
    rememberRecent(edge);
}

float EbNeuralLearning::progressNorm(float startX, float endX) {
    float const delta = endX - startX;
    if (!(delta > 0.0f))
        return 0.0f;
    return std::clamp(delta / 220.0f, 0.0f, 1.0f);
}

float EbNeuralLearning::successReward(int plannedDelay, float progress) {
    // Pass@d0 that merely survives observation must NOT dominate — that collapses
    // the scorer to always arm d=0. Delay>0 (real assist) gets the strong signal.
    if (plannedDelay <= 0)
        return 0.12f + 0.25f * progress;
    return 0.55f + 0.90f * progress;
}

float EbNeuralLearning::deathReward(float progress) {
    // Farther deaths are slightly less catastrophic (still negative).
    return -2.0f + 0.6f * progress;
}

float EbNeuralLearning::evidenceLearnScale(Evidence evidence) {
    switch (evidence) {
    case Evidence::TeacherSuggested:
        return 0.25f;
    case Evidence::SimulationVerified:
        return 0.85f;
    case Evidence::SurvivedObserved:
        return 1.0f;
    case Evidence::Death:
        return 1.0f;
    }
    return 0.25f;
}

float EbNeuralLearning::evidenceRewardScale(Evidence evidence) {
    switch (evidence) {
    case Evidence::TeacherSuggested:
        return 0.35f;
    case Evidence::SimulationVerified:
        return 0.70f;
    case Evidence::SurvivedObserved:
        return 1.0f;
    case Evidence::Death:
        return 1.0f;
    }
    return 0.35f;
}

float EbNeuralLearning::evidenceConfidenceTarget(Evidence evidence, int plannedDelay,
                                                 float progress) {
    switch (evidence) {
    case Evidence::TeacherSuggested:
        return plannedDelay > 0 ? 0.55f : 0.40f;
    case Evidence::SimulationVerified:
        return 1.0f;
    case Evidence::SurvivedObserved:
        return plannedDelay > 0 ? 1.0f : (0.45f + 0.2f * progress);
    case Evidence::Death:
        return 0.0f;
    }
    return 0.5f;
}

void EbNeuralLearning::rememberRecent(EdgeState const& edge) {
    if (!edge.valid || edge.plannedDelay < 0)
        return;
    RecentEdge& slot = m_recent[static_cast<size_t>(m_recentNext % static_cast<int>(m_recent.size()))];
    m_recentNext = (m_recentNext + 1) % static_cast<int>(m_recent.size());
    slot.valid = true;
    slot.tick = edge.tick;
    slot.action = edge.plannedDelay;
    slot.limit = edge.limit;
    slot.startX = edge.startX;
    std::memcpy(slot.features, edge.features, sizeof(slot.features));
}

void EbNeuralLearning::punishRecent(int deathTick, float endX) {
    // destroyPlayer hot path: session push only — no backprop / no grid rebuild.
    for (auto& recent : m_recent) {
        if (!recent.valid)
            continue;
        int const age = deathTick - recent.tick;
        if (age < 0 || age > 120)
            continue;
        float const decay = 1.0f - static_cast<float>(age) / 120.0f;
        float const progress = progressNorm(recent.startX, endX);
        float const reward = deathReward(progress) * (0.35f + 0.65f * decay);
        float next[EbNeuralModel::kFeatureDim];
        std::memcpy(next, recent.features, sizeof(next));
        EbNeuralSession::get().push(recent.features, recent.action, recent.limit, reward, next,
                                    true);
        recent.valid = false;
    }
}

void EbNeuralLearning::poll(PlayLayer* layer, int tick) {
    if (!active() || !m_loaded)
        return;
    for (auto& edge : m_edges) {
        if (!edge.valid || !edge.pending || !edge.fired)
            continue;
        if (tick - edge.tick < observationTicks)
            continue;
        if (edge.plannedDelay < 0) {
            edge.pending = false;
            continue;
        }
        float endX = edge.startX;
        if (layer) {
            auto* player = edge.player1 ? layer->m_player1 : layer->m_player2;
            if (player)
                endX = player->getPositionX();
        }
        // No fillFeatures mid-attempt (grid hitch). Identical next ⇒ supervised-only
        // (session skips TD bootstrap — Problem C). SurvivedObserved is still the
        // strongest evidence tier for rank/confidence.
        float next[EbNeuralModel::kFeatureDim];
        std::memcpy(next, edge.features, sizeof(next));
        float const progress = progressNorm(edge.startX, endX);
        Evidence const evidence = Evidence::SurvivedObserved;
        float const reward =
            successReward(edge.plannedDelay, progress) * evidenceRewardScale(evidence);
        EbNeuralSession::get().push(edge.features, edge.plannedDelay, edge.limit, reward, next,
                                    false);
        if (training()) {
            float const scale = evidenceLearnScale(evidence);
            if (edge.plannedDelay > 0)
                m_model.learnRank(edge.features, edge.plannedDelay, edge.limit,
                                  learningRate * scale);
            float scratch[EbNeuralModel::kFeatureDim];
            std::memcpy(scratch, edge.features, sizeof(scratch));
            EbNeuralModel::writeCandidateDelay(scratch, edge.plannedDelay);
            m_model.learnConfidence(
                scratch, evidenceConfidenceTarget(evidence, edge.plannedDelay, progress),
                learningRate * scale);
            // Replay TD with done=true so bootstrap is not s'=s.
            m_replay.push(edge.features, edge.plannedDelay, reward, next, true);
            ++updates;
            m_hasPriorOrWeights = true;
            m_deferTrain = true;
        }
        edge.pending = false;
    }
}

void EbNeuralLearning::death(bool player1, int tick) {
    // destroyPlayer hot path — push labels only. Backprop + disk = resetAttempt.
    if (!active() || !m_loaded)
        return;
    ++deaths;
    auto* layer = PlayLayer::get();
    float endX = 0.0f;
    if (layer) {
        auto* player = player1 ? layer->m_player1 : layer->m_player2;
        if (player)
            endX = player->getPositionX();
    }
    punishRecent(tick, endX);
    auto& edge = m_edges[player1 ? 0 : 1];
    if (!edge.valid || !edge.pending) {
        edge.pending = false;
        m_deferTrain = true;
        return;
    }
    if (edge.plannedDelay >= 0) {
        // No fillFeatures here — walking the section on death hitches hard.
        float next[EbNeuralModel::kFeatureDim];
        std::memcpy(next, edge.features, sizeof(next));
        float const progress = progressNorm(edge.startX, endX);
        float const reward = deathReward(progress);
        EbNeuralSession::get().push(edge.features, edge.plannedDelay, edge.limit, reward, next,
                                    true);
        if (training())
            m_replay.push(edge.features, edge.plannedDelay, reward, next, true);
    }
    edge.pending = false;
    m_deferTrain = true;
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
    std::string sessionReport;
    expect(EbNeuralSession::selfCheck(sessionReport), sessionReport.c_str());
    std::string autoplayReport;
    expect(EbNeuralAutoplay::selfCheck(autoplayReport), autoplayReport.c_str());
    std::string bootstrapReport;
    expect(EbNeuralBootstrap::selfCheck(bootstrapReport), bootstrapReport.c_str());
    // AiConfig / RoutePlanner selfChecks run in-game (need ClassManager); offline harness
    // covers Autoplay/Session/Model only.

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
