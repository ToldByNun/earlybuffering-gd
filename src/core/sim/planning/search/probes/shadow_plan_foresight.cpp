#include "shadow_plan_foresight.hpp"
#include "../../../../../config/stage_config.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../assist/planning/opportunity_probe.hpp"
#include "../../../foresight/long_foresight.hpp"
#include "../../../mode_survival.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../shadow_plan_context.hpp"
#include <algorithm>

void EbShadowPlanForesight::probe(EbShadowPlanContext& context) {
    if (!context.options.legacyPick) {
        int const h0 = std::max(EbLongForesight::kMinHorizon, context.scoreHorizon);
        int const lookCap = EbStageConfig::lookAheadCap();
        int const hMax = lookCap > 0 ? std::max(h0, lookCap)
                                     : std::min(320, h0 + h0 / 2 + EbLongForesight::kExtendStep);
        int const minBudget = h0 * 2 + EbOpportunityProbe::kMinBudgetForProbe;

        auto pathFromRun = [](EbShadowSimulator::RunResult const& rr) {
            EbLongForesight::PathStats s;
            s.died = rr.died;
            s.deathTick = rr.died ? rr.deathTick : -1;
            s.endX = rr.died ? rr.deathX : rr.endX;
            s.surviveTicks = EbShadowSurvivalRules::rankingSurviveTicks(rr);
            return s;
        };

        // Trajectory: lightweight hold/coast (same button semantics as traj viz).
        // ShadowPlayer: full runOne(d0) + runBaseline at long cap.
        auto sampleLight = [&](bool holdJump, int horizon) {
            EbLongForesight::PathStats s;
            if (!context.layer || !context.real || !context.simulator.m_shadow || horizon <= 0)
                return s;
            context.simulator.copyPlayerState(context.simulator.m_shadow, context.real);
            context.simulator.m_dead = false;
            context.simulator.m_jumped = false;
            context.simulator.m_unknown = false;
            context.simulator.m_activatedRing = false;
            context.simulator.m_ringCount = 0;
            context.simulator.m_deathObject = nullptr;
            auto* sh = context.simulator.m_shadow;
            sh->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = holdJump;
            for (int tick = 0; tick < horizon; ++tick) {
                if (context.tickBudget <= 0) {
                    context.plan.budgetHit = true;
                    break;
                }
                --context.tickBudget;
                float const stepDt = (tick == 0) ? context.firstDt : context.deltaTime;
                context.simulator.m_simAbsTick = context.simulator.m_simTickBase + tick;
                context.simulator.stepPhysics(context.layer, sh, stepDt, context.deltaTime);
                s.surviveTicks = tick + 1;
                s.endX = sh->getPositionX();
                if (context.simulator.m_dead) {
                    s.died = true;
                    s.deathTick = tick;
                    s.endX = sh->getPositionX();
                    break;
                }
                if (context.simulator.m_unknown) {
                    s.died = true;
                    s.deathTick = tick;
                    break;
                }
            }
            context.simulator.resetCaptureForNextRun(context.layer);
            return s;
        };

        auto samplePair = [&](int horizon, EbLongForesight::PathStats& pressOut,
                              EbLongForesight::PathStats& idleOut) {
            if (context.options.assistEngine == EbStageConfig::AssistEngineKind::Trajectory) {
                // down=press → action holds Jump; idle coasts. Release flips.
                pressOut = sampleLight(context.down, horizon);
                idleOut = sampleLight(!context.down, horizon);
                return;
            }
            EbShadowSimulator::Model longModel = context.model; // inherits continuePastClear
            longModel.cap = std::max(1, horizon);
            auto pressR = context.simulator.runOne(
                context.layer, context.real, context.down, 0, context.limit, context.deltaTime,
                context.firstDt, longModel, -1, context.zone.valid ? &context.zone : nullptr,
                &context.tickBudget);
            context.simulator.resetCaptureForNextRun(context.layer);
            auto idleR = context.simulator.runBaseline(context.layer, context.real, context.down,
                                                       context.deltaTime, context.firstDt,
                                                       longModel, &context.tickBudget);
            context.simulator.resetCaptureForNextRun(context.layer);
            pressOut = pathFromRun(pressR);
            idleOut = pathFromRun(idleR);
        };

        if (context.tickBudget >= minBudget && !context.wallExceeded()) {
            EbLongForesight::Input lfIn;
            lfIn.horizon = h0;
            samplePair(h0, lfIn.press, lfIn.idle);

            int const h1 =
                EbLongForesight::extendHorizon(h0, lfIn.press.died ? lfIn.press.deathTick : -1, hMax);
            if (h1 > h0 && context.tickBudget >= h1 * 2 && !context.wallExceeded()) {
                lfIn.hasExtended = true;
                lfIn.horizonExt = h1;
                samplePair(h1, lfIn.pressExt, lfIn.idleExt);
            }

            auto const lf = EbLongForesight::decide(lfIn);
            // Only invent idleSafer / force search when idle still lives.
            if (lf.idleSafer)
                context.plan.idleSafer = true;
            context.plan.longForesightNeedSearch = lf.needDelaySearch;
            EbAssistLog::debug(fmt::format(
                "PLAN id={} longForesight eng={} h={} ext={} "
                "pressDie={} idleDie={} idleSafer={} needSearch={} bothDead={} budget={}",
                context.plan.planId, EbStageConfig::assistEngineName(context.options.assistEngine),
                lf.horizonUsed, lf.extended ? 1 : 0, lf.pressSurvived ? 0 : 1,
                lf.idleSurvived ? 0 : 1, lf.idleSafer ? 1 : 0, lf.needDelaySearch ? 1 : 0,
                lf.bothDead ? 1 : 0, context.tickBudget));
        }
    }
}
