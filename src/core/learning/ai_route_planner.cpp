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
    m_lastPlanTick = -1;
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
    // Nova-confirmed base table at unk_180815298, scaled by unit=0.25/dt (sub_18030FC30).
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
    // EB layout still exposes press at continuous[8]; keep consistent for mixed priors.
    scratch[EbNeuralModel::kContinuousOffset + 8] = press ? 1.0f : 0.0f;
    float score = neural.model().confidence(scratch);
    score = std::clamp(score * cfg.scoreCalibration, 0.0f, 1.0f);
    return score;
}

static bool novaCanHold(GameModeKind kind) {
    // Nova: can_hold for packed gm ∈ {0,2,3,6,7} (cube/ball/ufo/spider/swing).
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

bool EbAiRoutePlanner::verifyCandidate(PlayLayer* layer, PlayerObject* player, bool press,
                                       int delay, int limit, float dt, EbAiConfig const& cfg,
                                       float& outProgress, bool& outClearsThreat) {
    outProgress = 0.f;
    outClearsThreat = false;
    if (!cfg.simulationEnabled)
        return true; // neural-only path (unsafe policies only)
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
    auto timedOut = [&]() {
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        return us >= cfg.searchTimeoutUs || us >= cfg.cpuBudgetUsPerTick;
    };

    EbPlanOptions prep{};
    (void)neural.prepare(layer, player, true, player1, tick, limit, dt, dt, prep);
    float const* features = nullptr;
    // Access last edge features via ranked path side effect — rebuild via prepare's edge.
    // prepare already filled edge; score using confidenceForDelay which copies that edge.
    // For multi-candidate we need the base feature vector: use confidenceForDelay's path.
    // Collect scores for press delays first.
    auto delays = enumerateDelays(cfg, limit);
    std::vector<EbAiScoredCandidate> scored;
    scored.reserve(delays.size() * 2);

    for (int delay : delays) {
        if (timedOut()) {
            route.budgetHit = true;
            break;
        }
        if (static_cast<int>(scored.size()) >= cfg.maxCandidates)
            break;
        if (cfg.generatePress) {
            float const n = neural.confidenceForDelay(delay);
            ++route.neuralEvals;
            float gate = cfg.confidenceThreshold;
            if (cfg.adaptiveThreshold) {
                // Soft adaptive: keep percentile of so-far scores as floor later.
                gate = std::min(gate, cfg.minCandidateConfidence);
            }
            if (!cfg.gatingEnabled || n >= gate || n >= cfg.minCandidateConfidence) {
                EbAiScoredCandidate c;
                c.delay = delay;
                c.press = true;
                c.neural = n;
                scored.push_back(c);
            }
        }
    }

    // Sort by neural descending (Nova keeps best).
    std::sort(scored.begin(), scored.end(), [](EbAiScoredCandidate const& a,
                                               EbAiScoredCandidate const& b) {
        return a.neural > b.neural;
    });
    if (cfg.gatingEnabled && cfg.adaptiveThreshold && !scored.empty()) {
        size_t idx = static_cast<size_t>(
            std::clamp(cfg.adaptivePercentile, 0.5f, 0.95f) * (scored.size() - 1));
        float const floor = scored[idx].neural;
        scored.erase(std::remove_if(scored.begin(), scored.end(),
                                    [&](EbAiScoredCandidate const& c) { return c.neural < floor; }),
                     scored.end());
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
        std::sort(scored.begin(), scored.end(), [](EbAiScoredCandidate const& a,
                                                   EbAiScoredCandidate const& b) {
            return a.neural > b.neural;
        });
    }

    // Beam: verify top candidates; extend with release / second press when depth > 1.
    class EbAiBeamNode {
    public:
        EbAiRouteEvent first{};
        float score = 0.f;
        bool ok = false;

    private:
    };
    std::vector<EbAiBeamNode> beam;
    int simCalls = 0;
    for (auto const& cand : scored) {
        if (timedOut() || simCalls >= cfg.maxSimCalls)
            break;
        float progress = 0.f;
        bool clears = false;
        bool ok = verifyCandidate(layer, player, cand.press, cand.delay, limit, dt, cfg, progress,
                                  clears);
        ++simCalls;
        if (!ok && cfg.pruneDeadBranches)
            continue;
        EbAiBeamNode node;
        node.first.kind = cand.press ? EbAiRouteEvent::Kind::Press : EbAiRouteEvent::Kind::Release;
        node.first.delay = cand.delay;
        node.first.neuralScore = cand.neural;
        node.first.verified = ok;
        node.first.clearsThreat = clears;
        node.first.holdTicks = 4;
        node.ok = ok;
        node.score = cfg.weightNeural * cand.neural + cfg.weightProgress * progress +
                     cfg.weightSafety * (clears ? 0.5f : 0.0f);
        if (!ok)
            node.score -= cfg.deathPenalty;
        beam.push_back(node);
        if (static_cast<int>(beam.size()) >= cfg.beamWidth)
            break;
    }

    if (beam.empty()) {
        // Orb soft fallback
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

    std::sort(beam.begin(), beam.end(),
              [](EbAiBeamNode const& a, EbAiBeamNode const& b) { return a.score > b.score; });
    auto best = beam.front();

    // Depth ≥ 2: plan hold/release when mode supports variable holds (Nova can_hold).
    GameModeKind const mode = EbStageConfig::kindOf(player);
    bool const canHold = novaCanHold(mode);
    if (cfg.maxRouteDepth >= 2 && cfg.generateRelease && canHold &&
        best.first.kind == EbAiRouteEvent::Kind::Press) {
        auto sample =
            EbShadowSimulator::get().sampleActionVsIdle(layer, player, true, limit, dt, dt);
        int hold = 4;
        if (sample.valid && !sample.unknown) {
            if (!sample.actionDied && sample.idleDied)
                hold = 10;
            else if (sample.actionDied && !sample.idleDied)
                hold = 2;
            else if (sample.actionEndX > sample.idleEndX + 8.f)
                hold = 8;
        }
        // Prefer a table delay ≥ 2 as hold length when adaptive (Nova schedule a7).
        if (cfg.adaptiveSampling && !delays.empty()) {
            for (int d : delays) {
                if (d >= 2 && d <= 40) {
                    hold = d;
                    break;
                }
            }
        }
        best.first.holdTicks = hold;
        if (cfg.maxRouteDepth >= 2) {
            EbAiRouteEvent release;
            release.kind = EbAiRouteEvent::Kind::Release;
            release.delay = best.first.delay + hold;
            release.neuralScore = best.first.neuralScore * 0.9f;
            release.verified = best.first.verified;
            // Optional third event: next press further out when depth allows.
            route.events.push_back(best.first);
            route.events.push_back(release);
            if (cfg.maxRouteDepth >= 3 && !timedOut() && simCalls < cfg.maxSimCalls) {
                int nextDelay = best.first.delay + hold + std::max(4, cfg.gapTicks / 2);
                if (nextDelay <= cfg.planningHorizon) {
                    float n2 = neural.confidenceForDelay(std::min(limit, nextDelay - best.first.delay));
                    if (!cfg.gatingEnabled || n2 >= cfg.confidenceThreshold * 0.85f) {
                        float progress = 0.f;
                        bool clears = false;
                        int relDelay = std::min(limit, nextDelay - best.first.delay);
                        bool ok = verifyCandidate(layer, player, true, relDelay, limit, dt, cfg,
                                                  progress, clears);
                        ++simCalls;
                        if (ok || clears) {
                            EbAiRouteEvent second;
                            second.kind = EbAiRouteEvent::Kind::Press;
                            second.delay = nextDelay;
                            second.neuralScore = n2;
                            second.holdTicks = hold;
                            second.verified = ok;
                            second.clearsThreat = clears;
                            route.events.push_back(second);
                        }
                    }
                }
            }
        }
    } else {
        route.events.push_back(best.first);
    }

    // Cap committed events
    if (static_cast<int>(route.events.size()) > cfg.maxCommittedEvents)
        route.events.resize(static_cast<size_t>(cfg.maxCommittedEvents));

    route.valid = !route.events.empty() &&
                  static_cast<int>(route.events.size()) >= cfg.minVerifiedPrefix;
    route.routeScore = best.score;
    route.startX = player->getPositionX();
    route.simCalls = simCalls;
    route.reason = route.valid ? "ok" : "prefix-short";
    if (route.budgetHit)
        route.reason = "budget";

    m_last = route;
    m_lastPlanTick = tick;
    m_lastPlanX = route.startX;
    ++plans;
    (void)features;
    (void)player1;
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
    if (fails == 0) {
        report = "AiRoutePlanner OK";
        return true;
    }
    report = std::string("AiRoutePlanner fails: ") + report;
    return false;
}
