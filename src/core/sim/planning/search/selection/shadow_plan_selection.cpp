#include "shadow_plan_selection.hpp"
#include "../../../../../config/stage_config.hpp"
#include "../../../../../debug/assist_log.hpp"
#include "../../../../../dev/evaluation/dev_reject_ledger.hpp"
#include "../../../../assist/contracts/assist_verdict.hpp"
#include "../../../../assist/planning/assist_pick_v1.hpp"
#include "../../../../assist/planning/assist_pick_v2.hpp"
#include "../../../../assist/planning/perfect_assist_policy.hpp"
#include "../../../foresight/long_foresight.hpp"
#include "../../../prefix_sim.hpp"
#include "../../../shadow_simulator.hpp"
#include "../../shadow_survival_rules.hpp"
#include "../../simulation_plan.hpp"
#include "../shadow_plan_context.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

bool EbShadowPlanSelection::choose(EbShadowPlanContext& context) {
    bool anyActivation = false;
    for (auto const& candidate : context.cands) {
        if (candidate.activatedRing)
            anyActivation = true;
    }
    context.orbIntent =
        context.orbAssist && (context.orbHint.found || context.spatialHint || anyActivation);
    context.ledgerOrbIntent = context.orbIntent;

    // Continuous ascentUseful: later Delay-worthy Alive beats d0 (not soft-dead).
    {
        auto const& c0ref = context.cands[0];
        for (int delay = 1; delay <= context.limit; ++delay) {
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            // Enable for press and release (hold-longer can beat immediate UP).
            if (context.simulator.classifyAscentUseful(
                    context.model.continuous, true, c0ref.viable, c0ref.surviveBin, c0ref.endX,
                    candidate.viable, candidate.surviveBin, candidate.endX, !candidate.raw.died)) {
                context.plan.ascentUseful = true;
                break;
            }
        }
    }

    // Wave: no Alive delay improves surviveBin / endX → Pass (avoid phase-shift).
    // Never discard a living delay when d0 died. idleSafer alone does not skip
    // this Pass — need an Alive later cand.
    {
        bool const isWave = context.real && context.real->m_isDart;
        int bestBin = 0;
        float bestGain = 0.f;
        float const baseEnd = context.cands[0].endX;
        bool anySavesFromD0Death = false;
        bool anyAliveLater = false;
        for (int delay = 0; delay <= context.limit; ++delay) {
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            bool const aliveWorthy = candidate.viable && !candidate.raw.died;
            if (!aliveWorthy)
                continue;
            if (candidate.surviveBin > bestBin)
                bestBin = candidate.surviveBin;
            float const gain = candidate.endX - baseEnd;
            if (gain > bestGain)
                bestGain = gain;
            if (delay > 0) {
                anyAliveLater = true;
                if (context.cands[0].raw.died)
                    anySavesFromD0Death = true;
            }
        }
        // idleSafer skips futile Pass only when an Alive later escape exists.
        bool const idleHoldsEscape = context.plan.idleSafer && anyAliveLater;
        if (!anySavesFromD0Death && !idleHoldsEscape &&
            context.simulator.shouldWaveFutileDelayPass(isWave, context.orbIntent, bestBin,
                                                        bestGain, context.plan.foresightOrbPad,
                                                        context.down)) {
            context.plan.action = PlanAction::Pass;
            context.plan.reason = AssistReason::PassWaveFutile;
            context.plan.delay = 0;
            context.plan.nowIsSafe = context.cands[0].viable;
            context.plan.threatCleared = context.cands[0].clearsThreat || !context.zone.valid;
            context.plan.jumpTick = context.cands[0].jumpTick;
            context.plan.activatedRing = context.cands[0].activatedRing;
            context.plan.threatZone = context.zone;
            EbAssistLog::debug(fmt::format(
                "PLAN id={} Pass wave-futile-delay verdict={} bin={} gain={:.1f} budgetHit={}",
                context.plan.planId, EbAssistVerdict::name(context.plan.reason), bestBin, bestGain,
                context.plan.budgetHit));
            context.publishLedger(0, true, EbDevRejectLedger::EarlyExit::WaveFutile);
            return true;
        }
    }

    for (auto& candidate : context.cands) {
        candidate.orbUseful = false;
        if (!context.orbAssist || !candidate.activatedRing || !candidate.viable)
            continue;
        // Viable activation is useful for ranking — do not require spatialHint
        // when d0 already "safe" but missed the ring (orb-miss G1).
        if (!context.cands[0].clearsThreat) {
            candidate.orbUseful = true;
            continue;
        }
        if (!context.cands[0].activatedRing) {
            candidate.orbUseful = true;
            continue;
        }
        candidate.orbUseful = true;
    }

    // Optional: cand must also clear threat under firstDt ±3%.
    // Commit 10: jitter only Top-2 ranked picks (never walk the whole pool).

    // pickFn returns next best delay; jitter capped at PrefixSim::kJitterTopK attempts.
    auto pickWithJitterTop2 = [&](auto&& pickFn) -> int {
        int attempts = 0;
        for (;;) {
            int const pick = pickFn();
            if (pick < 0)
                return -1;
            int const releaseAfter = context.cands[static_cast<size_t>(pick)].requireHold ? 40 : -1;
            if (!context.options.robustJitter)
                return pick;
            if (!EbPrefixSim::jitterAttemptAllowed(attempts, EbPrefixSim::kJitterTopK)) {
                // Top-2 already failed jitter — stop (do not burn the pool).
                return -1;
            }
            ++attempts;
            if (context.jitterOk(pick, releaseAfter))
                return pick;
            context.cands[static_cast<size_t>(pick)].viable = false;
            context.cands[static_cast<size_t>(pick)].robustKFailed = true;
        }
    };

    int actMin = -1, actMax = -1;
    for (int delay = 0; delay <= context.limit; ++delay) {
        auto const& candidate = context.cands[static_cast<size_t>(delay)];
        if (candidate.viable && candidate.activatedRing) {
            if (actMin < 0)
                actMin = delay;
            actMax = delay;
        }
    }
    // Window-cap: activation band never reported past limit.
    if (actMin > context.limit)
        actMin = context.limit;
    if (actMax > context.limit)
        actMax = context.limit;
    context.plan.dMin = actMin;
    context.plan.dMax = actMax;
    context.ledgerActMin = actMin;
    context.ledgerActMax = actMax;

    // Live choose: v1 = AssistPickV1 (latest-robust); v2 = AssistPickV2 (foresight-first).
    bool const usePickV2 = !context.options.legacyPick;
    std::vector<EbAssistPickV1::CandView> pickViewsV1(
        static_cast<size_t>(std::max(0, context.limit)) + 1u);
    std::vector<EbAssistPickV2::CandView> pickViewsV2(
        static_cast<size_t>(std::max(0, context.limit)) + 1u);
    auto syncPickViews = [&]() {
        for (int delay = 0; delay <= context.limit; ++delay) {
            auto const& candidate = context.cands[static_cast<size_t>(delay)];
            if (usePickV2) {
                auto& v = pickViewsV2[static_cast<size_t>(delay)];
                v.viable = candidate.viable;
                v.activatedRing = candidate.activatedRing;
                v.orbUseful = candidate.orbUseful;
                v.surviveBin = candidate.surviveBin;
                v.surviveTicks = candidate.surviveTicks;
                v.endX = candidate.endX;
                v.died = candidate.raw.died;
            } else {
                auto& v = pickViewsV1[static_cast<size_t>(delay)];
                v.viable = candidate.viable;
                v.activatedRing = candidate.activatedRing;
            }
        }
    };
    auto pickEarliestRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
        syncPickViews();
        if (usePickV2) {
            return EbAssistPickV2::pickEarliestRobust(pickViewsV2.data(), context.limit,
                                                    context.robustK, pool, needActivation);
        }
        return EbAssistPickV1::pickEarliestRobust(pickViewsV1.data(), context.limit, context.robustK,
                                                pool, needActivation);
    };
    auto pickLatestRobust = [&](std::vector<int> const& pool, bool needActivation) -> int {
        syncPickViews();
        if (usePickV2) {
            return EbAssistPickV2::pickLatestRobust(pickViewsV2.data(), context.limit,
                                                  context.robustK, pool, needActivation);
        }
        return EbAssistPickV1::pickLatestRobust(pickViewsV1.data(), context.limit, context.robustK,
                                              pool, needActivation);
    };
    auto applyChosenMeta = [&](int pick) {
        if (pick < 0 || pick > context.limit)
            return;
        auto const& candidate = context.cands[static_cast<size_t>(pick)];
        context.plan.threatCleared = candidate.clearsThreat;
        context.plan.activatedRing = candidate.activatedRing;
        context.plan.requireHold = candidate.requireHold;
        context.plan.orbUseful = candidate.orbUseful;
    };

    context.chosen = -1;
    context.doPass = false;
    bool haveActViable = actMin >= 0;

    if (context.orbIntent && haveActViable) {
        std::vector<int> pool;
        for (int delay = actMin; delay <= actMax; ++delay)
            pool.push_back(delay);
        int const offset = context.robustK;
        auto tryPick = [&](std::vector<int> const& p) -> int {
            return pickWithJitterTop2([&]() { return pickEarliestRobust(p, true); });
        };
        if (actMax - actMin >= 2 * offset) {
            std::vector<int> interior;
            for (int delay = actMin + offset; delay <= actMax - offset; ++delay)
                interior.push_back(delay);
            context.chosen = tryPick(interior);
        }
        if (context.chosen < 0)
            context.chosen = tryPick(pool);
        if (context.chosen >= 0) {
            applyChosenMeta(context.chosen);
            context.plan.requireOrb = true;
        }
    } else if (usePickV2) {
        // V2 foresight-first (Hybrid bake wins never reach this Live path).
        EbAssistPickV2::Context ctx;
        ctx.idleSafer = context.plan.idleSafer;
        ctx.ascentUseful = context.plan.ascentUseful;
        ctx.longForesightNeedSearch = context.plan.longForesightNeedSearch;
        ctx.latencyWeight = context.latencyWeight;

        auto runV2Choose = [&]() -> EbAssistPickV2::Result {
            syncPickViews();
            return EbAssistPickV2::choose(pickViewsV2.data(), context.limit, context.robustK, false,
                                        -1, -1, ctx);
        };

        // Jitter top-2 around foresight pick; then re-choose on surviving cands.
        context.chosen = pickWithJitterTop2([&]() { return runV2Choose().chosen; });
        {
            auto result = runV2Choose();
            if (result.chosen >= 0) {
                context.chosen = result.chosen;
                context.doPass = result.doPass;
            } else if (context.chosen >= 0 && context.chosen <= context.limit &&
                       context.cands[static_cast<size_t>(context.chosen)].viable) {
                context.doPass = (context.chosen == 0);
            } else {
                context.chosen = -1;
                context.doPass = false;
            }
            if (context.chosen >= 0)
                applyChosenMeta(context.chosen);
            if (context.doPass && context.chosen == 0) {
                context.plan.threatCleared = true;
                context.plan.nowIsSafe = true;
            }
        }
        EbAssistLog::debug(fmt::format(
            "PLAN id={} pickV2 foresight chosen={} pass={} idleSafer={} lfSearch={} ascent={}",
            context.plan.planId, context.chosen, context.doPass ? 1 : 0,
            context.plan.idleSafer ? 1 : 0, context.plan.longForesightNeedSearch ? 1 : 0,
            context.plan.ascentUseful ? 1 : 0));
    } else {
        // v1: d0 viable → Pass. Else latest ±k-robust in window (miss-assist wait).
        if (context.cands[0].viable) {
            context.chosen = pickWithJitterTop2([&]() { return context.cands[0].viable ? 0 : -1; });
            if (context.chosen == 0) {
                context.doPass = true;
                context.plan.threatCleared = true;
                context.plan.nowIsSafe = true;
            } else {
                std::vector<int> pool;
                for (int delay = 0; delay <= context.limit; ++delay)
                    pool.push_back(delay);
                context.chosen =
                    pickWithJitterTop2([&]() { return pickLatestRobust(pool, false); });
                applyChosenMeta(context.chosen);
            }
        } else {
            std::vector<int> pool;
            for (int delay = 0; delay <= context.limit; ++delay)
                pool.push_back(delay);
            context.chosen = pickWithJitterTop2([&]() { return pickLatestRobust(pool, false); });
            applyChosenMeta(context.chosen);
        }
    }

    // G1 window-cap assert.
    if (context.chosen > context.limit)
        context.chosen = context.limit;
    // Hard gate: never arm a non-viable Delay (Commit 9: Landing/Recoverable may
    // have raw.died but still be Delay-safe via ModeSurvival).
    if (context.chosen > 0 && context.chosen <= context.limit) {
        auto const& ch = context.cands[static_cast<size_t>(context.chosen)];
        if (!ch.viable || ch.diedBeforeAction || ch.unknownTrunc || ch.toggleUnknown) {
            if (ch.toggleUnknown || (ch.unknownTrunc && context.hadToggleTargets)) {
                context.sawToggleUnknown = true;
            }
            EbAssistLog::debug(
                fmt::format("PLAN id={} reject lethal/unknown Delay +{} → Pass toggleUnk={}",
                            context.plan.planId, context.chosen, context.sawToggleUnknown ? 1 : 0));
            context.chosen = -1;
            context.doPass = false;
            context.plan.requireOrb = false;
        }
    }
    if (context.chosen < 0) {
        // Commit 11: never soft-trust budgetHit into Delay via partial actMin.
        if (context.plan.budgetHit && actMin >= 0 && actMin <= context.limit &&
            context.cands[static_cast<size_t>(actMin)].viable &&
            EbPerfectAssistPolicy::allowArmDelay(true, context.plan.budgetHit,
                                               /*softTrustPartial=*/true)) {
            context.chosen = actMin;
            context.doPass = false;
            applyChosenMeta(context.chosen);
            context.plan.requireOrb = true;
            EbAssistLog::debug(fmt::format("PLAN id={} budget-partial actMin={} pick={}",
                                         context.plan.planId, actMin, context.chosen));
        }
    }

    // Continuous miss-assist: idle safer + ascent → Delay only with Alive later.
    // Never force Delay on soft-dead. InterveneGate: ascentUseful alone is NOT
    // "improves" when d0 is already Delay-worthy Alive (help-don't-hurt).
    if (context.plan.idleSafer && context.plan.ascentUseful &&
        (context.doPass || context.chosen <= 0)) {
        std::vector<int> pool;
        for (int delay = 1; delay <= context.limit; ++delay)
            pool.push_back(delay);
        int forced = pickLatestRobust(pool, false);
        if (forced > 0) {
            auto const& fc = context.cands[static_cast<size_t>(forced)];
            bool const forcedWorthy = fc.viable && !fc.raw.died;
            if (forcedWorthy) {
                bool const d0Ok = context.cands[0].viable && !context.cands[0].raw.died;
                bool improves = !d0Ok;
                if (d0Ok) {
                    improves = fc.surviveBin > context.cands[0].surviveBin ||
                               fc.endX > context.cands[0].endX + EbShadowSimulator::kFutileEndXGain;
                }
                if (EbPerfectAssistPolicy::interveneOk(d0Ok, true, improves)) {
                    context.chosen = forced;
                    context.doPass = false;
                    applyChosenMeta(context.chosen);
                    EbAssistLog::debug(
                        fmt::format("PLAN id={} idleSafer+ascent → Delay +{} (was Pass/hopeless)",
                                    context.plan.planId, context.chosen));
                }
            } else {
                EbAssistLog::debug(fmt::format("PLAN id={} idleSafer+ascent skip — no Alive later "
                                             "(forced={} viable={} died={})",
                                             context.plan.planId, forced, fc.viable ? 1 : 0,
                                             fc.raw.died ? 1 : 0));
            }
        }
    }
    // Re-assert Delay-worthy gate after idleSafer / budget-partial.
    if (context.chosen > 0 && context.chosen <= context.limit) {
        auto const& ch = context.cands[static_cast<size_t>(context.chosen)];
        if (!ch.viable || ch.raw.died || ch.diedBeforeAction || ch.unknownTrunc ||
            ch.toggleUnknown) {
            if (ch.toggleUnknown || (ch.unknownTrunc && context.hadToggleTargets)) {
                context.sawToggleUnknown = true;
            }
            context.chosen = -1;
            context.doPass = false;
            context.plan.requireOrb = false;
        }
    }

    // assist-verify=ab formerly coin-flipped Legacy vs New; now logs active pick.
    if (EbStageConfig::assistVerifyAb()) {
        EbAssistLog::debug(fmt::format("VERIFY id={} ab-noop pick={} chosen={} doPass={}",
                                     context.plan.planId, usePickV2 ? "v2" : "v1", context.chosen,
                                     context.doPass ? 1 : 0));
    }

    // Hold/release window for the chosen Delay: how long can they hold, when
    // to let go without dying. Covers ship/wave hold, robot, orb dash holds.
    if (context.chosen > 0 && context.down && context.tickBudget > 0 && !context.wallExceeded()) {
        auto const& base = context.cands[static_cast<size_t>(context.chosen)];
        int earliest = -1;
        int latest = -1;
        int bestRel = base.releaseAfter;
        int bestBin = base.surviveBin;
        float bestEnd = base.endX;
        int tried = 0;

        // Seed from branch that already won.
        if (base.releaseAfter >= 0 && base.viable) {
            earliest = base.releaseAfter;
            latest = base.releaseAfter;
        }

        for (int rel : EbShadowSurvivalRules::HOLD_PROBE_RELEASES) {
            if (tried >= EbShadowSurvivalRules::HOLD_PROBE_MAX)
                break;
            if (context.tickBudget <= 0 || context.wallExceeded())
                break;
            if (base.releaseAfter >= 0 && rel == base.releaseAfter)
                continue;
            ++tried;
            auto result = context.runEngine(context.real, context.chosen, rel,
                                            context.zone.valid ? &context.zone : nullptr);
            context.simulator.resetCaptureForNextRun(context.layer);
            ++context.plan.candidatesTried;

            // Hold branch — same Delay-viable rule (soft bake vs Live Alive-only).
            if (!EbShadowSurvivalRules::runResultDelaySafe(result, context.zone, context.model,
                                                         context.real, context.requireAlive))
                continue;

            int const sTicks = EbShadowSurvivalRules::rankingSurviveTicks(result);
            int const sBin = EbShadowSurvivalRules::surviveBinOf(sTicks);
            if (earliest < 0 || rel < earliest)
                earliest = rel;
            if (latest < 0 || rel > latest)
                latest = rel;
            if (bestRel < 0 || sBin > bestBin ||
                (sBin == bestBin && result.endX > bestEnd + 0.5f) ||
                (sBin == bestBin && std::fabs(result.endX - bestEnd) <= 0.5f &&
                 (context.model.continuous ? rel < bestRel : false))) {
                bestRel = rel;
                bestBin = sBin;
                bestEnd = result.endX;
            }
        }

        context.plan.holdScanTried = tried;
        if (earliest >= 0) {
            context.plan.releaseEarliest = earliest;
            context.plan.releaseLatest = latest >= earliest ? latest : earliest;
            if (bestRel < 0) {
                bestRel = context.simulator.pickPreferredRelease(context.plan.releaseEarliest,
                                                                 context.plan.releaseLatest,
                                                                 context.model.continuous);
            }
            context.plan.releaseAfter = bestRel;
            if (context.plan.releaseAfter >= 40 || base.requireHold) {
                context.plan.requireHold = true;
            }
        } else if (base.releaseAfter >= 0) {
            context.plan.releaseAfter = base.releaseAfter;
            context.plan.releaseEarliest = base.releaseAfter;
            context.plan.releaseLatest = base.releaseAfter;
        }
    }

    return false;
}
