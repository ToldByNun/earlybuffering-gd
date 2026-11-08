#include "shadow_sequence_planner.hpp"
#include "../../../config/stage_config.hpp"
#include "../../../debug/assist_log.hpp"
#include "../../assist/contracts/assist_verdict.hpp"
#include "../../assist/planning/assist_horizon.hpp"
#include "../../bake/bake_map.hpp"
#include "../shadow_simulator.hpp"
#include "../sim_trust.hpp"
#include "shadow_survival_rules.hpp"
#include "simulation_plan.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

SimPlan ShadowSequencePlanner::planSequence(ShadowSimulator& simulator, PlayLayer* layer,
                                            PlayerObject* real, bool down, int limit,
                                            float deltaTime, float firstDt,
                                            PlanOptions const& options) {
    auto single = simulator.plan(layer, real, down, limit, deltaTime, firstDt, options);
    int const depth = std::clamp(options.smartDepth, 1, 3);

    // Smart multi-jump / bake-chain only when smartDepth > 1.
    // Former New default (exit-predict at depth 1) removed with New choose.
    std::vector<int> allowed;
    if (single.action == PlanAction::Pass) {
        allowed.push_back(0);
    } else if (!single.viableDelays.empty()) {
        for (int delayValue : single.viableDelays) {
            if (delayValue < 0 || delayValue > limit)
                continue;
            allowed.push_back(delayValue);
        }
        if (allowed.empty())
            allowed.push_back(single.delay);
    } else {
        // Fallback when plan skipped cand eval (rare): only the chosen delay.
        allowed.push_back(std::clamp(single.delay, 0, limit));
    }
    if (depth <= 1 || !down) {
        single.smartDepthUsed = 1;
        return single;
    }

    ShadowSimulator::Model model = simulator.modelFor(real, limit, options.capMul);
    model.continuePastClear = options.requireAliveForDelay;
    // ScoreHorizon from AssistHorizon (long only when trusted).
    int horizon = options.smartHorizon;
    if (horizon <= 0) {
        bool const trustOk = SimTrust::get().okAt(real->getPositionX());
        horizon = AssistHorizon::resolve(StageConfig::kindOf(real), trustOk, limit, options.capMul,
                                         0, StageConfig::lookAheadCap())
                      .scoreHorizon;
        horizon = std::max(horizon, model.cap);
    }
    horizon = std::clamp(horizon, 32, 320);
    int comboBudget = std::clamp(options.smartCombos, 8, 256);

    int slot = simulator.captureSnapshot(real);
    if (slot < 0)
        return single;

    class Best {
    public:
        bool found = false;
        int d0 = 0;
        int score = -1;
        bool survived = false;

    private:
    } best;

    auto pushTap = [&](std::vector<InputEvent>& tl, int pressTick, bool tap) {
        tl.push_back(InputEvent{pressTick, true});
        if (tap)
            tl.push_back(InputEvent{pressTick + 2, false});
    };
    bool const tap = model.tapRelease;
    int minGap = tap ? 6 : 4;
    int step = std::max(2, horizon / 40);
    int combos = 0;

    // Smart bake-chain: fix follow-up clicks from BakeMap when available.
    GameModeKind const kind = StageConfig::kindOf(real);
    bool const player2 = layer && layer->m_player2 && real == layer->m_player2;
    int const chainDepth = std::max(0, depth - 1);
    auto followUps = (BakeMap::get().loaded() && chainDepth > 0)
                         ? BakeMap::get().nextClicksAfter(real->getPositionX(), true, player2, kind,
                                                          std::min(chainDepth, 2))
                         : std::vector<BakedClick const*>{};

    float const speed =
        std::max(0.5f, real->m_playerSpeed * 311.2f * std::max(deltaTime, 1.f / 240.f));
    std::vector<BakedClick const*> chain;
    for (auto const* candidate : followUps) {
        if (!candidate)
            continue;
        if (candidate->mode != kind)
            break;
        float const deltaX = candidate->x - real->getPositionX();
        int const eta = static_cast<int>(std::lround(deltaX / speed));
        if (eta <= 0 || eta >= horizon)
            continue;
        chain.push_back(candidate);
        if (static_cast<int>(chain.size()) >= chainDepth)
            break;
    }
    bool const useBakeChain = !chain.empty();

    for (int d0 : allowed) {
        if (combos >= comboBudget)
            break;

        if (useBakeChain) {
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            for (auto const* candidate : chain) {
                float const deltaX = candidate->x - real->getPositionX();
                int tFollow = std::max(d0 + minGap, static_cast<int>(std::lround(deltaX / speed)));
                int const midFrame = (candidate->earliest + candidate->latest) / 2;
                int const approxNow = options.attemptStartTick;
                if (midFrame > approxNow) {
                    tFollow = std::max(d0 + minGap, midFrame - approxNow);
                }
                if (tFollow >= horizon)
                    continue;
                pushTap(tl, tFollow, tap);
            }
            auto rr = simulator.replay(layer, slot, 0, tl, deltaTime, horizon, false, 0);
            ++combos;
            bool const survived = ShadowSurvivalRules::exitSurvivedHonest(rr.died, rr.unknown);
            int sc = survived ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing)
                sc += 5000;
            if (!chain.empty() && survived) {
                float const targetX = chain.front()->x;
                if (rr.endX + BakeMap::LOOKUP_X_TOL >= targetX)
                    sc += 20000;
                else
                    sc -= 10000;
            }
            if (!best.found || sc > best.score) {
                best = {true, d0, sc, survived};
            }
            continue;
        }

        {
            // Single-press exit probe: which first delay actually clears ahead?
            std::vector<InputEvent> tl;
            pushTap(tl, d0, tap);
            auto rr = simulator.replay(layer, slot, 0, tl, deltaTime, horizon, false, 0);
            ++combos;
            bool const survived = ShadowSurvivalRules::exitSurvivedHonest(rr.died, rr.unknown);
            int sc = survived ? 100000 : rr.deathTick * 50;
            float const lat = std::max(0.f, options.latencyWeight);
            sc += static_cast<int>(rr.endX - lat * static_cast<float>(d0));
            if (rr.activatedRing)
                sc += 5000;
            if (!best.found || sc > best.score) {
                best = {true, d0, sc, survived};
            }
        }
        if (depth >= 2) {
            for (int d1 = d0 + minGap; d1 < horizon && combos < comboBudget; d1 += step) {
                std::vector<InputEvent> tl2;
                pushTap(tl2, d0, tap);
                pushTap(tl2, d1, tap);
                auto rr2 = simulator.replay(layer, slot, 0, tl2, deltaTime, horizon, false, 0);
                ++combos;
                bool const survived2 =
                    ShadowSurvivalRules::exitSurvivedHonest(rr2.died, rr2.unknown);
                int sc2 = survived2 ? 100000 : rr2.deathTick * 50;
                float const lat2 = std::max(0.f, options.latencyWeight);
                sc2 += static_cast<int>(rr2.endX - lat2 * static_cast<float>(d0));
                if (rr2.activatedRing)
                    sc2 += 5000;
                if (!best.found || sc2 > best.score) {
                    best = {true, d0, sc2, survived2};
                }
            }
        }
    }

    single.smartDepthUsed = depth;
    single.smartCombosUsed = combos;
    // Only accept a predicted exit that actually survived the horizon.
    if (!best.found || !best.survived) {
        AssistLog::debug(fmt::format("planSequence id={} no surviving exit — keep plan delay={}",
                                     single.planId, single.delay));
        return single;
    }
    if (std::find(allowed.begin(), allowed.end(), best.d0) == allowed.end())
        return single;

    if (best.d0 == 0) {
        single.action = PlanAction::Pass;
        single.reason = AssistReason::PassD0Safe;
        single.delay = 0;
        single.nowIsSafe = true;
    } else {
        single.action = PlanAction::Delay;
        single.delay = best.d0;
        single.nowIsSafe = false;
        if (!AssistVerdict::isDelay(single.reason)) {
            single.reason =
                AssistVerdict::inferDelayReason(single.activatedRing, single.requireOrb, false);
        }
    }
    AssistLog::debug(fmt::format(
        "planSequence id={} depth={} d0={} verdict={} allowed={} combos={} bakeChain={}",
        single.planId, depth, best.d0, AssistVerdict::name(single.reason), allowed.size(), combos,
        useBakeChain));
    return single;
}
