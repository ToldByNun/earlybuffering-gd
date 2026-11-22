#include "ai_route_planner.hpp"
#include "../../app/class_manager.hpp"
#include "../../config/stage_config.hpp"
#include "../assist/assist_controller.hpp"
#include "../sim/planning/simulation_plan.hpp"
#include "../sim/shadow_simulator.hpp"
#include "neural_learning.hpp"
#include "neural_model.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

EbAiRoutePlanner& EbAiRoutePlanner::get() {
    return EbClassManager::get().service<EbAiRoutePlanner>();
}

void EbAiRoutePlanner::invalidate(char const* why) {
    ++invalidations;
    m_last.clear();
    m_last.reason = why ? why : "invalidated";
    m_lastSeedCands.clear();
    m_lastExpandJob = {};
    m_lastExpandResult = {};
    m_occupancy.clear();
    m_lastPlanTick = -1;
}

std::string EbAiRoutePlanner::diagnoseLastExpand() const {
    if (m_lastExpandJob.id == 0 && m_lastSeedCands.empty())
        return "expand: none";
    std::string line = EbRouteCandSemantics::diagnoseJob(m_lastExpandJob);
    line += fmt::format(" seeds={} trunc={} stuck={} occ={} reason={}",
                        static_cast<int>(m_lastSeedCands.size()),
                        m_lastExpandResult.truncated ? 1 : 0,
                        m_lastExpandResult.stuckProbeOnly ? 1 : 0, m_occupancy.claimedCount(),
                        m_lastExpandResult.reason ? m_lastExpandResult.reason : "?");
    return line;
}

EbRouteExpandCore::Result EbAiRoutePlanner::expandSeedShared(PlayLayer* layer, PlayerObject* player,
                                                             EbRouteCandSemantics::Cand const& seed,
                                                             int tick, int limit, float dt) {
    EbRouteExpandCore::Result empty;
    empty.reason = "bad-args";
    if (!layer || !player || dt <= 0.f)
        return empty;
    auto& cfg = EbAiConfig::get();
    EbRouteExpandCore::Options opts;
    opts.budgetUs = std::max(200, cfg.simBudgetUs);
    opts.horizonTicks = std::max(limit, cfg.planningHorizon);
    opts.maxSimCalls = cfg.maxSimCalls;
    opts.generateRelease = cfg.generateRelease;
    opts.holdChoices = {2, 4, 6, 8, 12, 16};
    if (opts.holdChoices.size() > 4)
        opts.holdChoices.resize(4);

    auto probe = [&](EbAiRouteEvent::Kind kind, int delayTicks, int holdTicks,
                     int /*simIndex*/) -> EbRouteExpandCore::SimOutcome {
        (void)holdTicks;
        EbRouteExpandCore::SimOutcome out;
        bool const down = kind == EbAiRouteEvent::Kind::Press;
        EbPlanOptions planOpts{};
        planOpts.budgetUs = std::max(200, cfg.simBudgetUs / 4);
        planOpts.tickBudget = cfg.maxSimTicks;
        planOpts.requireAliveForDelay = cfg.strictVerify;
        planOpts.attemptStartTick = tick;
        auto const vr = EbShadowSimulator::get().verifyDelay(layer, player, down, delayTicks, limit,
                                                             dt, dt, planOpts);
        out.ok = vr.ok;
        out.unknown = vr.unknown;
        out.budgetHit = vr.budgetHit;
        out.clearsThreat = vr.clearsThreat;
        out.progress = vr.ok ? 1.f : (vr.clearsThreat ? 0.55f : 0.f);
        if (!vr.ok && vr.clearsThreat && !vr.diedBeforeAction) {
            out.ok = true;
            out.progress = 0.55f;
        }
        return out;
    };

    auto result = EbRouteExpandCore::expandAroundSeed(m_nextJobId++, seed, tick, opts, m_occupancy,
                                                      probe);
    m_lastExpandResult = result;
    m_lastExpandJob = result.job;
    return result;
}

EbRouteExpandCore::Result EbAiRoutePlanner::stuckSimCore(PlayLayer* layer, PlayerObject* player,
                                                         int tick, int delayTicks, int limit,
                                                         float dt) {
    EbRouteExpandCore::Result empty;
    empty.reason = "bad-args";
    empty.stuckProbeOnly = true;
    if (!layer || !player || dt <= 0.f)
        return empty;
    auto& cfg = EbAiConfig::get();
    EbRouteExpandCore::Options opts;
    opts.budgetUs = std::max(200, std::min(cfg.simBudgetUs, 1500));
    opts.horizonTicks = std::max(limit, 24);
    opts.maxSimCalls = 1;
    opts.generateRelease = false;

    auto probe = [&](EbAiRouteEvent::Kind kind, int delay, int, int) -> EbRouteExpandCore::SimOutcome {
        EbRouteExpandCore::SimOutcome out;
        bool const down = kind == EbAiRouteEvent::Kind::Press;
        EbPlanOptions planOpts{};
        planOpts.budgetUs = opts.budgetUs;
        planOpts.tickBudget = std::min(96, cfg.maxSimTicks);
        planOpts.requireAliveForDelay = true;
        planOpts.attemptStartTick = tick;
        auto const vr =
            EbShadowSimulator::get().verifyDelay(layer, player, down, delay, limit, dt, dt, planOpts);
        out.ok = vr.ok;
        out.unknown = vr.unknown;
        out.budgetHit = vr.budgetHit;
        out.clearsThreat = vr.clearsThreat;
        out.progress = vr.ok ? 1.f : 0.f;
        return out;
    };

    auto result =
        EbRouteExpandCore::stuckSimCoreProbe(m_nextJobId++, tick, delayTicks, opts, m_occupancy, probe);
    m_lastExpandResult = result;
    m_lastExpandJob = result.job;
    return result;
}

bool EbAiRoutePlanner::needsReplan(int tick, float liveX) const {
    auto const& cfg = EbAiConfig::get();
    if (!m_last.valid)
        return true;
    if (m_lastPlanTick < 0)
        return true;
    if (tick - m_lastPlanTick >= cfg.replanEveryTicks)
        return true;
    if (std::fabs(liveX - m_lastPlanX) > cfg.divergenceTolerancePx)
        return true;
    return false;
}

std::vector<EbInputEvent>
EbAiRoutePlanner::timelineFromEvents(std::vector<EbAiRouteEvent> const& events, int planTick) {
    std::vector<EbInputEvent> timeline;
    timeline.reserve(events.size());
    int base = std::max(0, planTick);
    for (auto const& ev : events) {
        EbInputEvent input;
        input.tick = base + std::max(0, ev.delay);
        input.down = (ev.kind == EbAiRouteEvent::Kind::Press);
        timeline.push_back(input);
    }
    std::sort(timeline.begin(), timeline.end(),
              [](EbInputEvent const& a, EbInputEvent const& b) {
                  if (a.tick != b.tick)
                      return a.tick < b.tick;
                  return a.down && !b.down; // press before release on same tick
              });
    return timeline;
}

bool EbAiRoutePlanner::passesGate(float neural, EbAiConfig const& cfg) {
    if (!cfg.gatingEnabled)
        return true;
    // Both floors must clear — minCandidateConfidence alone must not bypass threshold.
    return neural >= cfg.confidenceThreshold && neural >= cfg.minCandidateConfidence;
}

void EbAiRoutePlanner::keepTopPercentile(std::vector<float>& descendingScores, float percentile,
                                         float& outFloor) {
    outFloor = 0.f;
    if (descendingScores.empty())
        return;
    float const keep = std::clamp(percentile, 0.05f, 1.0f);
    // Keep the best `keep` fraction: floor is the score at that cutoff index.
    size_t const keepCount =
        std::max<size_t>(1, static_cast<size_t>(std::ceil(keep * descendingScores.size())));
    size_t const idx = std::min(keepCount, descendingScores.size()) - 1;
    outFloor = descendingScores[idx];
}

std::vector<int> EbAiRoutePlanner::enumerateDelays(EbAiConfig const& cfg, int limit) const {
    std::vector<int> out;
    int const hi = std::min(cfg.maxDelay, std::min(limit, cfg.candidateWindow));
    int const lo = std::max(0, cfg.minDelay);
    if (!cfg.adaptiveSampling) {
        int const step = std::max(1, cfg.sampleStep);
        for (int d = lo; d <= hi; d += step)
            out.push_back(d);
        if (out.empty())
            out.push_back(0);
        return out;
    }
    static constexpr int kNovaDelayBase[] = {1,  2,  3,  4,  5,  6,  8,  10, 12, 15, 18, 22, 27,
                                             33, 40, 50, 62, 78, 96, 120, 150, 190, 240, 300, 380,
                                             480};
    float const dt = EbAssistController::get().simDt();
    float const unit = 0.25f / std::max(dt > 0.f ? dt : (1.f / 240.f), 0.001f);
    int last = 0;
    for (int base : kNovaDelayBase) {
        int v = static_cast<int>(std::lround(static_cast<float>(base) * unit));
        if (v < 2)
            v = 1;
        if (v < lo)
            continue;
        if (v > hi)
            break;
        if (out.empty() || v > last) {
            out.push_back(v);
            last = v;
        }
    }
    if (out.empty())
        out.push_back(std::clamp(lo, 0, hi));
    return out;
}

float EbAiRoutePlanner::encodeAndScore(float const* baseFeatures, int delay, bool press) const {
    if (!baseFeatures)
        return 0.f;
    auto& neural = EbNeuralLearning::get();
    if (!neural.loaded())
        return 0.f;
    float scratch[EbNeuralModel::kFeatureDim];
    std::memcpy(scratch, baseFeatures, sizeof(scratch));
    auto const& cfg = EbAiConfig::get();
    bool const nova = cfg.delayEncoding == EbAiConfig::DelayEncoding::Log1p;
    EbNeuralModel::writeCandidateForPolicy(scratch, delay, press, nova);
    scratch[EbNeuralModel::kContinuousOffset + 8] = press ? 1.0f : 0.0f;
    float score = neural.model().confidence(scratch);
    score = std::clamp(score * cfg.scoreCalibration, 0.0f, 1.0f);
    return score;
}

static bool novaCanHold(GameModeKind kind) {
    switch (kind) {
    case GameModeKind::Cube:
    case GameModeKind::Ball:
    case GameModeKind::Ufo:
    case GameModeKind::Spider:
    case GameModeKind::Swing:
        return true;
    default:
        return false;
    }
}

EbAiRoutePlanner::EbAiVerifyOutcome
EbAiRoutePlanner::verifyTimeline(PlayLayer* layer, int snapshotSlot, int planTick,
                                 std::vector<EbAiRouteEvent> const& events, float dt,
                                 EbAiConfig const& cfg, int budgetUs) {
    EbAiVerifyOutcome out;
    if (!cfg.simulationEnabled) {
        out.ok = true;
        out.progress = 1.f;
        return out;
    }
    if (!layer || snapshotSlot < 0 || events.empty() || dt <= 0.f)
        return out;
    auto timeline = timelineFromEvents(events, planTick);
    int const horizon =
        std::min(cfg.planningHorizon, std::max(24, events.back().delay + cfg.maxSimTicks / 6));
    // Cap each replay by the remaining plan wall (Nova µs hard wall).
    int const remUs = std::max(200, budgetUs > 0 ? budgetUs : cfg.simBudgetUs);
    auto rr = EbShadowSimulator::get().replay(layer, snapshotSlot, planTick, timeline, dt, horizon,
                                              false, 0, remUs);
    ++verifyTries;
    ++m_last.simCalls;
    // Replay truncates to unknown on µs wall — treat as budget, not a soft reject.
    if (rr.unknown && !rr.died && remUs > 0 && remUs <= cfg.simBudgetUs) {
        out.budgetHit = true;
        out.unknown = true;
        return out;
    }
    if (rr.unknown && cfg.rejectUnknown) {
        out.unknown = true;
        return out;
    }
    out.unknown = rr.unknown;
    if (!rr.died) {
        out.ok = true;
        out.progress = 1.f;
        ++verifyHits;
        return out;
    }
    // Partial credit if we got further than the first press delay.
    if (rr.deathTick > events.front().delay) {
        out.clearsThreat = true;
        out.progress = 0.55f;
        if (!cfg.strictVerify)
            out.ok = true;
    }
    return out;
}

bool EbAiRoutePlanner::verifyCandidate(PlayLayer* layer, PlayerObject* player, bool press,
                                       int delay, int limit, float dt, EbAiConfig const& cfg,
                                       float& outProgress, bool& outClearsThreat) {
    outProgress = 0.f;
    outClearsThreat = false;
    if (!cfg.simulationEnabled)
        return true;
    EbPlanOptions opts{};
    opts.budgetUs = cfg.simBudgetUs;
    opts.tickBudget = cfg.maxSimTicks;
    opts.requireAliveForDelay = cfg.strictVerify;
    opts.attemptStartTick = EbAssistController::get().attemptTick();
    auto const vr = EbShadowSimulator::get().verifyDelay(layer, player, press, delay, limit, dt, dt,
                                                         opts);
    ++verifyTries;
    ++m_last.simCalls;
    if (vr.budgetHit) {
        m_last.budgetHit = true;
        ++budgetHits;
        return false;
    }
    if (cfg.rejectUnknown && vr.unknown)
        return false;
    outClearsThreat = vr.clearsThreat;
    if (vr.ok) {
        ++verifyHits;
        outProgress = 1.0f;
        return true;
    }
    if (vr.clearsThreat && !vr.diedBeforeAction) {
        outProgress = 0.55f;
        return true;
    }
    return false;
}

EbAiRoute EbAiRoutePlanner::plan(PlayLayer* layer, PlayerObject* player, bool player1, int tick,
                                 int limit, float dt) {
    EbAiRoute route;
    route.planTick = tick;
    route.reason = "fail";
    auto& cfg = EbAiConfig::get();
    if (!layer || !player || dt <= 0.f) {
        route.reason = "bad-args";
        m_last = route;
        return route;
    }
    if (!cfg.enabled) {
        route.reason = "disabled";
        m_last = route;
        return route;
    }
    auto& neural = EbNeuralLearning::get();
    if (!EbNeuralLearning::active() || !neural.loaded()) {
        route.reason = "no-model";
        m_last = route;
        return route;
    }

    auto const t0 = std::chrono::steady_clock::now();
    auto elapsedUs = [&]() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count());
    };
    // Nova-style hard wall: searchTimeout ∧ cpuBudget ∧ maxAiCpu (frame slice).
    int const hardWallUs =
        std::min({cfg.searchTimeoutUs, cfg.cpuBudgetUsPerTick, cfg.maxAiCpuUsPerTick});
    auto timedOut = [&]() { return elapsedUs() >= hardWallUs; };
    auto remainingSimUs = [&]() {
        int const rem = hardWallUs - elapsedUs();
        return std::clamp(std::min(cfg.simBudgetUs, rem), 200, cfg.simBudgetUs);
    };

    EbPlanOptions prep{};
    (void)neural.prepare(layer, player, true, player1, tick, limit, dt, dt, prep);

    auto delays = enumerateDelays(cfg, limit);
    std::vector<EbAiScoredCandidate> scored;
    scored.reserve(delays.size());

    for (int delay : delays) {
        if (timedOut()) {
            route.budgetHit = true;
            break;
        }
        if (static_cast<int>(scored.size()) >= cfg.maxCandidates)
            break;
        if (!cfg.generatePress)
            break;
        float const n = neural.confidenceForDelay(delay);
        ++route.neuralEvals;
        if (passesGate(n, cfg)) {
            EbAiScoredCandidate c;
            c.delay = delay;
            c.press = true;
            c.neural = n;
            scored.push_back(c);
        }
    }

    std::sort(scored.begin(), scored.end(),
              [](EbAiScoredCandidate const& a, EbAiScoredCandidate const& b) {
                  return a.neural > b.neural;
              });
    if (cfg.gatingEnabled && cfg.adaptiveThreshold && !scored.empty()) {
        std::vector<float> scores;
        scores.reserve(scored.size());
        for (auto const& c : scored)
            scores.push_back(c.neural);
        float floor = 0.f;
        keepTopPercentile(scores, cfg.adaptivePercentile, floor);
        scored.erase(std::remove_if(scored.begin(), scored.end(),
                                    [&](EbAiScoredCandidate const& c) { return c.neural < floor; }),
                     scored.end());
        // Always keep at least minRetained of the original top scores.
        if (static_cast<int>(scored.size()) < cfg.minRetained) {
            scored.clear();
            for (int delay : delays) {
                if (static_cast<int>(scored.size()) >= cfg.minRetained)
                    break;
                EbAiScoredCandidate c;
                c.delay = delay;
                c.press = true;
                c.neural = neural.confidenceForDelay(delay);
                scored.push_back(c);
            }
            std::sort(scored.begin(), scored.end(),
                      [](EbAiScoredCandidate const& a, EbAiScoredCandidate const& b) {
                          return a.neural > b.neural;
                      });
            if (static_cast<int>(scored.size()) > cfg.minRetained)
                scored.resize(static_cast<size_t>(cfg.minRetained));
        }
    }
    if (static_cast<int>(scored.size()) > cfg.maxRetained)
        scored.resize(static_cast<size_t>(cfg.maxRetained));
    if (scored.empty() && cfg.lowConfidenceFallback) {
        for (int delay : delays) {
            if (static_cast<int>(scored.size()) >= cfg.fallbackCandidateCount)
                break;
            EbAiScoredCandidate c;
            c.delay = delay;
            c.press = true;
            c.neural = neural.confidenceForDelay(delay);
            scored.push_back(c);
        }
        std::sort(scored.begin(), scored.end(),
                  [](EbAiScoredCandidate const& a, EbAiScoredCandidate const& b) {
                      return a.neural > b.neural;
                  });
    }

    // AP-NL3: materialize semantic seed cands for expand handoff / Live diagnose.
    m_lastSeedCands.clear();
    m_lastSeedCands.reserve(scored.size());
    for (auto const& cand : scored) {
        m_lastSeedCands.push_back(
            EbRouteCandSemantics::makeSeed(m_nextCandId++, cand.delay, cand.neural, 4));
    }

    int const startSlot = EbShadowSimulator::get().captureSnapshot(player);
    if (startSlot < 0 && cfg.simulationEnabled) {
        route.reason = "no-snapshot";
        m_last = route;
        return route;
    }

    // AP-NL4: fresh occupancy for this plan — claimed ticks cannot double-arm.
    m_occupancy.clear();

    GameModeKind const mode = EbStageConfig::kindOf(player);
    bool const canHold = novaCanHold(mode);
    std::vector<int> holdChoices = {2, 4, 6, 8, 10, 12, 16, 24};
    if (cfg.adaptiveSampling) {
        holdChoices.clear();
        for (int d : delays) {
            if (d >= 2 && d <= 40)
                holdChoices.push_back(d);
            if (static_cast<int>(holdChoices.size()) >= 6)
                break;
        }
        if (holdChoices.empty())
            holdChoices = {4, 8};
    }

    // Depth-1 seeds: light verifyDelay (Nova single-click path), not full replay.
    std::vector<EbAiBeamNode> beam;
    int simCalls = 0;
    for (auto const& cand : scored) {
        if (timedOut() || simCalls >= cfg.maxSimCalls)
            break;
        if (!m_occupancy.tryClaim(tick + cand.delay))
            continue;
        EbAiRouteEvent press;
        press.kind = EbAiRouteEvent::Kind::Press;
        press.delay = cand.delay;
        press.neuralScore = cand.neural;
        press.holdTicks = holdChoices.empty() ? 4 : holdChoices.front();
        EbAiBeamNode node;
        node.events.push_back(press);
        float progress = 0.f;
        bool clearsThreat = false;
        EbPlanOptions opts{};
        opts.budgetUs = remainingSimUs();
        opts.tickBudget = cfg.maxSimTicks;
        opts.requireAliveForDelay = cfg.strictVerify;
        opts.attemptStartTick = tick;
        auto const vr = EbShadowSimulator::get().verifyDelay(layer, player, true, cand.delay, limit,
                                                            dt, dt, opts);
        ++simCalls;
        ++verifyTries;
        ++m_last.simCalls;
        if (vr.budgetHit) {
            route.budgetHit = true;
            ++budgetHits;
            break; // hard wall — stop seeding
        }
        bool ok = vr.ok;
        if (!ok && vr.clearsThreat && !vr.diedBeforeAction) {
            progress = 0.55f;
            clearsThreat = true;
            ok = true;
        } else if (ok) {
            progress = 1.f;
            clearsThreat = vr.clearsThreat;
            ++verifyHits;
        }
        if (!ok && cfg.pruneDeadBranches) {
            m_occupancy.release(tick + cand.delay);
            continue;
        }
        node.events.front().verified = ok;
        node.events.front().clearsThreat = clearsThreat;
        node.ok = ok;
        node.holding = ok;
        node.lastAbsDelay = press.delay;
        node.score = cfg.weightNeural * cand.neural + cfg.weightProgress * progress +
                     cfg.weightSafety * (clearsThreat ? 0.5f : 0.0f);
        if (!ok)
            node.score -= cfg.deathPenalty;
        beam.push_back(std::move(node));
        if (static_cast<int>(beam.size()) >= cfg.beamWidth)
            break;
    }

    if (beam.empty()) {
        // AP-NL4: stuck/memory sim-core — no neural ranking, verifyDelay only.
        if (cfg.simulationEnabled) {
            int const stuckDelay = delays.empty() ? 0 : delays.front();
            auto stuck = stuckSimCore(layer, player, tick, stuckDelay, limit, dt);
            if (EbRouteCandSemantics::canArmJob(stuck.job)) {
                route.events = EbRouteCandSemantics::toRouteEvents(stuck.job);
                route.valid = true;
                route.reason = "stuck-sim-core";
                route.routeScore = 0.f;
                route.startX = player->getPositionX();
                route.simCalls = stuck.simCalls;
                m_last = route;
                m_lastPlanTick = tick;
                m_lastPlanX = route.startX;
                ++plans;
                return route;
            }
        }
        if (cfg.orbCandidates &&
            EbAssistControllerImplementation::nearOrbOrPadHint(layer, player, limit, dt)) {
            int delayHint = delays.empty() ? 0 : delays.front();
            float conf = neural.confidenceForDelay(delayHint);
            if (conf >= cfg.orbHintConfidence) {
                EbAiRouteEvent ev;
                ev.kind = EbAiRouteEvent::Kind::Press;
                ev.delay = delayHint;
                ev.neuralScore = conf;
                ev.holdTicks = 4;
                ev.verified = false;
                route.events.push_back(ev);
                route.valid = true;
                route.reason = "orb-hint";
                route.routeScore = conf;
                route.startX = player->getPositionX();
                m_last = route;
                m_lastPlanTick = tick;
                m_lastPlanX = route.startX;
                ++plans;
                return route;
            }
        }
        route.reason = "no-beam";
        m_last = route;
        return route;
    }

    // Expand beam up to maxRouteDepth. Every extension re-verifies the FULL sequence
    // from the start snapshot (so Press→Release→Press cannot skip a lethal middle).
    int depth = 1;
    while (depth < cfg.maxRouteDepth && !timedOut() && simCalls < cfg.maxSimCalls) {
        std::vector<EbAiBeamNode> nextBeam;
        std::sort(beam.begin(), beam.end(),
                  [](EbAiBeamNode const& a, EbAiBeamNode const& b) { return a.score > b.score; });
        int branches = 0;
        for (auto const& parent : beam) {
            if (timedOut() || simCalls >= cfg.maxSimCalls || branches >= cfg.maxBranches)
                break;
            // Always keep the parent as a complete route candidate.
            nextBeam.push_back(parent);

            if (parent.holding && cfg.generateRelease && canHold) {
                for (int hold : holdChoices) {
                    if (timedOut() || simCalls >= cfg.maxSimCalls || branches >= cfg.maxBranches)
                        break;
                    EbAiBeamNode child = parent;
                    EbAiRouteEvent release;
                    release.kind = EbAiRouteEvent::Kind::Release;
                    release.delay = parent.lastAbsDelay + hold;
                    if (release.delay > cfg.planningHorizon)
                        continue;
                    if (!m_occupancy.tryClaim(tick + release.delay))
                        continue;
                    release.neuralScore = parent.events.back().neuralScore * 0.9f;
                    if (!child.events.empty() && child.events.back().kind == EbAiRouteEvent::Kind::Press)
                        child.events.back().holdTicks = hold;
                    child.events.push_back(release);
                    EbAiVerifyOutcome vo = verifyTimeline(layer, startSlot, tick, child.events, dt,
                                                         cfg, remainingSimUs());
                    ++simCalls;
                    ++branches;
                    if (vo.budgetHit) {
                        route.budgetHit = true;
                        ++budgetHits;
                        break;
                    }
                    if (!vo.ok && cfg.pruneDeadBranches) {
                        m_occupancy.release(tick + release.delay);
                        continue;
                    }
                    child.events.back().verified = vo.ok;
                    child.events.back().clearsThreat = vo.clearsThreat;
                    child.ok = vo.ok;
                    child.holding = false;
                    child.lastAbsDelay = release.delay;
                    child.score = parent.score * 0.85f + cfg.weightProgress * vo.progress +
                                  cfg.weightSafety * (vo.ok ? 0.35f : 0.f);
                    if (!vo.ok)
                        child.score -= cfg.deathPenalty;
                    nextBeam.push_back(std::move(child));
                }
            } else if (!parent.holding && cfg.generatePress) {
                // Next press after a release (or depth-1 press without hold path).
                int const gap = std::max(4, cfg.gapTicks / 2);
                for (auto const& cand : scored) {
                    if (timedOut() || simCalls >= cfg.maxSimCalls || branches >= cfg.maxBranches)
                        break;
                    int nextDelay = parent.lastAbsDelay + gap + cand.delay;
                    if (nextDelay > cfg.planningHorizon)
                        continue;
                    if (!m_occupancy.tryClaim(tick + nextDelay))
                        continue;
                    EbAiBeamNode child = parent;
                    EbAiRouteEvent press;
                    press.kind = EbAiRouteEvent::Kind::Press;
                    press.delay = nextDelay;
                    press.neuralScore = cand.neural;
                    press.holdTicks = holdChoices.empty() ? 4 : holdChoices.front();
                    child.events.push_back(press);
                    EbAiVerifyOutcome vo = verifyTimeline(layer, startSlot, tick, child.events, dt,
                                                         cfg, remainingSimUs());
                    ++simCalls;
                    ++branches;
                    if (vo.budgetHit) {
                        route.budgetHit = true;
                        ++budgetHits;
                        break;
                    }
                    if (!vo.ok && cfg.pruneDeadBranches)
                        continue;
                    child.events.back().verified = vo.ok;
                    child.events.back().clearsThreat = vo.clearsThreat;
                    child.ok = vo.ok;
                    child.holding = vo.ok;
                    child.lastAbsDelay = nextDelay;
                    child.score = parent.score * 0.7f + cfg.weightNeural * cand.neural +
                                  cfg.weightProgress * vo.progress;
                    if (!vo.ok)
                        child.score -= cfg.deathPenalty;
                    nextBeam.push_back(std::move(child));
                }
            }
        }
        if (nextBeam.empty())
            break;
        std::sort(nextBeam.begin(), nextBeam.end(),
                  [](EbAiBeamNode const& a, EbAiBeamNode const& b) { return a.score > b.score; });
        if (static_cast<int>(nextBeam.size()) > cfg.beamWidth)
            nextBeam.resize(static_cast<size_t>(cfg.beamWidth));
        beam = std::move(nextBeam);
        ++depth;
    }

    std::sort(beam.begin(), beam.end(),
              [](EbAiBeamNode const& a, EbAiBeamNode const& b) { return a.score > b.score; });
    auto const& best = beam.front();
    route.events = best.events;
    if (static_cast<int>(route.events.size()) > cfg.maxCommittedEvents)
        route.events.resize(static_cast<size_t>(cfg.maxCommittedEvents));

    int verifiedPrefix = 0;
    for (auto const& ev : route.events) {
        if (!ev.verified)
            break;
        ++verifiedPrefix;
    }
    route.valid = !route.events.empty() && verifiedPrefix >= cfg.minVerifiedPrefix;
    route.routeScore = best.score;
    route.startX = player->getPositionX();
    route.simCalls = simCalls;
    route.reason = route.valid ? "ok" : "prefix-short";
    if (route.budgetHit)
        route.reason = "budget";

    // AP-NL3: record expand job lifecycle from the committed event sequence.
    {
        EbRouteCandSemantics::Cand seed =
            m_lastSeedCands.empty()
                ? EbRouteCandSemantics::makeSeed(m_nextCandId++, 0, 0.f, 4)
                : m_lastSeedCands.front();
        auto job = EbRouteCandSemantics::beginExpand(m_nextJobId++, seed, tick,
                                                     cfg.planningHorizon);
        job.events.clear();
        job.budgetHit = route.budgetHit;
        for (auto const& ev : route.events) {
            EbRouteCandSemantics::Cand event;
            event.id = m_nextCandId++;
            event.kind = ev.kind;
            event.delayTicks = ev.delay;
            event.holdTicks = ev.holdTicks;
            event.neuralScore = ev.neuralScore;
            event.clearsThreat = ev.clearsThreat;
            event.simOk = ev.verified;
            event.budgetHit = route.budgetHit;
            event.expandJobId = job.id;
            event.state = ev.verified ? EbRouteCandSemantics::LifeState::PartiallyChecked
                                     : EbRouteCandSemantics::LifeState::Rejected;
            if (event.delayTicks > job.lastEventDelay)
                job.lastEventDelay = event.delayTicks;
            job.events.push_back(event);
        }
        job.state = EbRouteCandSemantics::LifeState::Expanding;
        EbRouteCandSemantics::finalizeExpand(job);
        // AP-NL4 honesty: truncated/budget expand must never remain ShadowVerified.
        m_lastExpandResult = {};
        m_lastExpandResult.job = job;
        m_lastExpandResult.simCalls = simCalls;
        m_lastExpandResult.truncated = route.budgetHit;
        if (route.budgetHit || m_lastExpandResult.truncated) {
            job.budgetHit = true;
            if (job.state == EbRouteCandSemantics::LifeState::ShadowVerified) {
                job.state = EbRouteCandSemantics::LifeState::PartiallyChecked;
                for (auto& ev : job.events) {
                    if (ev.state == EbRouteCandSemantics::LifeState::ShadowVerified)
                        ev.state = EbRouteCandSemantics::LifeState::PartiallyChecked;
                }
            }
            m_lastExpandResult.job = job;
            m_lastExpandResult.truncated = true;
        }
        m_lastExpandJob = job;
        m_lastExpandResult.reason =
            EbRouteExpandCore::honestyOk(m_lastExpandResult)
                ? (m_lastExpandResult.truncated ? "partial-trunc" : "ok")
                : "dishonest";
        // Autoplay may still use prefix-valid routes; ShadowVerified is the arm gate (NL5b).
        if (route.valid && !EbRouteCandSemantics::canArmJob(job) &&
            job.state == EbRouteCandSemantics::LifeState::PartiallyChecked)
            route.reason = route.budgetHit ? "budget" : "partial-verify";
    }

    m_last = route;
    m_lastPlanTick = tick;
    m_lastPlanX = route.startX;
    ++plans;
    return route;
}

bool EbAiRoutePlanner::selfCheck(std::string& report) {
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };
    EbAiRoutePlanner planner;
    expect(!planner.lastRoute().valid, "empty-default");
    planner.invalidate("test");
    expect(planner.invalidations == 1, "invalidate");
    expect(planner.needsReplan(0, 0.f), "needs-replan-empty");

    auto& gateCfg = EbAiConfig::get();
    bool const savedGate = gateCfg.gatingEnabled;
    float const savedThresh = gateCfg.confidenceThreshold;
    float const savedMin = gateCfg.minCandidateConfidence;
    gateCfg.gatingEnabled = true;
    gateCfg.confidenceThreshold = 0.6f;
    gateCfg.minCandidateConfidence = 0.3f;
    expect(!passesGate(0.4f, gateCfg), "gate-blocks-below-threshold");
    expect(passesGate(0.65f, gateCfg), "gate-allows-above-both");
    gateCfg.minCandidateConfidence = 0.7f;
    expect(!passesGate(0.65f, gateCfg), "gate-requires-min-too");
    gateCfg.gatingEnabled = savedGate;
    gateCfg.confidenceThreshold = savedThresh;
    gateCfg.minCandidateConfidence = savedMin;

    std::vector<float> scores = {0.9f, 0.8f, 0.7f, 0.6f, 0.5f};
    float floor = 0.f;
    keepTopPercentile(scores, 0.8f, floor);
    // Best 80% of 5 = 4 → floor at index 3 = 0.6
    expect(std::fabs(floor - 0.6f) < 0.001f, "percentile-floor");

    std::vector<EbAiRouteEvent> events;
    EbAiRouteEvent press;
    press.kind = EbAiRouteEvent::Kind::Press;
    press.delay = 5;
    press.holdTicks = 4;
    press.verified = true;
    events.push_back(press);
    EbAiRouteEvent release;
    release.kind = EbAiRouteEvent::Kind::Release;
    release.delay = 9;
    release.verified = true;
    events.push_back(release);
    EbAiRouteEvent press2;
    press2.kind = EbAiRouteEvent::Kind::Press;
    press2.delay = 20;
    press2.verified = false;
    events.push_back(press2);
    auto tl = timelineFromEvents(events, 100);
    expect(tl.size() == 3, "timeline-size");
    expect(tl[0].tick == 105 && tl[0].down, "timeline-press1");
    expect(tl[1].tick == 109 && !tl[1].down, "timeline-release");
    expect(tl[2].tick == 120 && tl[2].down, "timeline-press2");
    // Release is its own verified event — not copied from the press.
    expect(events[1].verified && events[1].kind == EbAiRouteEvent::Kind::Release,
           "release-own-flag");

    std::string semanticsReport;
    expect(EbRouteCandSemantics::selfCheck(semanticsReport), "route-cand-semantics");
    std::string expandCoreReport;
    expect(EbRouteExpandCore::selfCheck(expandCoreReport), "route-expand-core");

    if (fails == 0) {
        report = "AiRoutePlanner OK";
        return true;
    }
    report = std::string("AiRoutePlanner fails: ") + report;
    return false;
}
