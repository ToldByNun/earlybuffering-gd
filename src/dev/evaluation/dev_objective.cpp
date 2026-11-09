#include "dev_objective.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <limits>

char const* EbDevObjective::kindId(Kind offset) {
    switch (offset) {
    case Kind::SurviveThenEndX:
        return "survive_then_endx_v1";
    case Kind::PlannerMirror:
        return "planner_mirror_v1";
    case Kind::HorizonClear:
        return "horizon_clear_v1";
    }
    return "survive_then_endx_v1";
}

bool EbDevObjective::parseKind(std::string const& s, Kind& out) {
    if (s == "survive_then_endx_v1") {
        out = Kind::SurviveThenEndX;
        return true;
    }
    if (s == "planner_mirror_v1") {
        out = Kind::PlannerMirror;
        return true;
    }
    if (s == "horizon_clear_v1") {
        out = Kind::HorizonClear;
        return true;
    }
    return false;
}

void EbDevObjective::classifyPrimaryAndTail(Sample& s, int delay, int horizonTicks, int tailTicks,
                                          bool died, int deathTick, float endX, bool unknown,
                                          bool budgetHit, bool aborted, int ticksRan) {
    if (delay < 0)
        delay = 0;
    if (horizonTicks < 0)
        horizonTicks = 0;
    if (tailTicks < 0)
        tailTicks = 0;

    s.delay = delay;
    s.unknown = unknown;
    s.budgetHit = budgetHit;
    s.aborted = aborted;
    s.ticksRan = ticksRan;
    s.endX = endX;
    s.deathTick = died ? deathTick : -1;
    s.tailFailed = false;
    s.tailDeathTick = -1;
    s.tailEndX = 0.f;
    s.tailComplete = false;
    s.survivedPrimary = false;
    s.threatCleared = false;

    if (s.inconclusive()) {
        return;
    }

    int const primaryEnd = delay + horizonTicks; // exclusive end index
    int const totalEnd = primaryEnd + tailTicks;

    if (died) {
        if (deathTick < primaryEnd) {
            // Died during wait or primary horizon.
            s.survivedPrimary = false;
            s.endX = endX;
            return;
        }
        // Survived primary; died in tail.
        s.survivedPrimary = true;
        s.threatCleared = true;
        s.tailFailed = true;
        s.tailDeathTick = deathTick;
        s.tailEndX = endX;
        s.tailComplete = true;
        // endX for primary: we only have death-in-tail endX; keep it.
        return;
    }

    // No death.
    if (ticksRan >= primaryEnd) {
        s.survivedPrimary = true;
        s.threatCleared = true;
        if (tailTicks <= 0) {
            s.tailComplete = true;
            s.tailFailed = false;
            s.tailEndX = endX;
        } else if (ticksRan >= totalEnd) {
            s.tailComplete = true;
            s.tailFailed = false;
            s.tailEndX = endX;
        } else {
            // Truncated before tail finished (should be budget/abort — already handled).
            s.tailComplete = false;
        }
    } else {
        s.survivedPrimary = false;
    }
}

std::optional<int> EbDevObjective::bestSurvivingDelay(std::vector<Sample> const& samples) {
    bool found = false;
    int bestDelay = -1;
    float bestEndX = -std::numeric_limits<float>::infinity();
    for (auto const& s : samples) {
        if (s.inconclusive() || !s.survivedPrimary)
            continue;
        if (!found || s.endX > bestEndX + 1e-4f ||
            (std::fabs(s.endX - bestEndX) <= 1e-4f && s.delay < bestDelay)) {
            found = true;
            bestDelay = s.delay;
            bestEndX = s.endX;
        }
    }
    if (!(!found)) {
        return bestDelay;
    }
    return std::nullopt;
}

void EbDevObjective::applyClaimsGate(Result& r, Meta const& meta) {
    if (meta.claimsAllowed)
        return;
    r.hasBestSurviving = false;
    r.bestSurvivingDelay = -1;
    r.hasBestEvaluated = false;
    r.bestEvaluatedDelay = -1;
    r.idealEligible = false;
    r.hasIdealDelay = false;
    r.idealDelay = -1;
    r.idealBlockedReason = "claims_suppressed";
}

void EbDevObjective::fillIdealGate(Result& r, Meta const& meta, Kind kind, bool clearSetNonEmpty) {
    r.idealEligible = false;
    r.hasIdealDelay = false;
    r.idealDelay = -1;
    r.idealBlockedReason.clear();

    if (kind != Kind::HorizonClear) {
        r.idealBlockedReason = "objective_not_horizon_clear";
        return;
    }
    if (!meta.claimsAllowed) {
        r.idealBlockedReason = "claims_suppressed";
        return;
    }
    if (meta.fidelity != DevFidelity::Reproduced) {
        r.idealBlockedReason = "fidelity";
        return;
    }
    if (!meta.gridFull || meta.objectivePartial) {
        r.idealBlockedReason = "grid_partial";
        return;
    }
    if (!meta.tailsComplete) {
        r.idealBlockedReason = "tail_incomplete";
        return;
    }
    if (!meta.clockOk) {
        r.idealBlockedReason = "clock";
        return;
    }
    if (!clearSetNonEmpty) {
        r.idealBlockedReason = "no_clear_survivor";
        return;
    }
    r.idealEligible = true;
}

bool EbDevObjective::clearEligible(Sample const& s) {
    return s.survivedPrimary && !s.tailFailed && s.threatCleared && !s.unknown && !s.budgetHit &&
           !s.aborted && s.tailComplete;
}

EbDevObjective::Result EbDevObjective::evaluateSurviveThenEndX(std::vector<Sample> const& samples,
                                                           Meta const& meta) {
    Result r;
    r.kind = Kind::SurviveThenEndX;
    r.objectiveId = kindId(Kind::SurviveThenEndX);
    r.horizonTicks = meta.horizonTicks;
    r.tailTicks = meta.tailTicks;
    r.objectivePartial = meta.objectivePartial;

    auto surv = bestSurvivingDelay(samples);
    if (surv) {
        r.hasBestSurviving = true;
        r.bestSurvivingDelay = *surv;
    }

    // Lexicographic over eligible: survivedPrimary, endX, delay.
    bool found = false;
    int bestDelay = -1;
    bool bestSurv = false;
    float bestEndX = -std::numeric_limits<float>::infinity();
    for (auto const& s : samples) {
        if (s.inconclusive())
            continue;
        bool const better = !found || (s.survivedPrimary && !bestSurv) ||
                            (s.survivedPrimary == bestSurv &&
                             (s.endX > bestEndX + 1e-4f ||
                              (std::fabs(s.endX - bestEndX) <= 1e-4f && s.delay < bestDelay)));
        if (!better)
            continue;
        found = true;
        bestDelay = s.delay;
        bestSurv = s.survivedPrimary;
        bestEndX = s.endX;
    }
    if (found) {
        r.hasBestEvaluated = true;
        r.bestEvaluatedDelay = bestDelay;
    }

    fillIdealGate(r, meta, Kind::SurviveThenEndX, false);
    applyClaimsGate(r, meta);
    return r;
}

EbDevObjective::Result EbDevObjective::evaluatePlannerMirror(std::vector<Sample> const& samples,
                                                         Meta const& meta) {
    Result r;
    r.kind = Kind::PlannerMirror;
    r.objectiveId = kindId(Kind::PlannerMirror);
    r.horizonTicks = meta.horizonTicks;
    r.tailTicks = meta.tailTicks;
    r.objectivePartial = meta.objectivePartial;

    auto surv = bestSurvivingDelay(samples);
    if (surv) {
        r.hasBestSurviving = true;
        r.bestSurvivingDelay = *surv;
    }

    // Survive-bin: survived > later death > earlier death; then endX - λ·delay.
    auto surviveBin = [](Sample const& s) -> int {
        if (s.survivedPrimary)
            return 2;
        if (!s.inconclusive() && s.deathTick >= 0)
            return 1;
        return 0;
    };
    auto score = [&](Sample const& s) -> float {
        return s.endX - meta.latencyWeight * static_cast<float>(s.delay);
    };

    bool found = false;
    int bestDelay = -1;
    int bestBin = -1;
    float bestScore = -std::numeric_limits<float>::infinity();
    int bestDeathTick = -1;
    for (auto const& s : samples) {
        if (s.inconclusive())
            continue;
        int const bin = surviveBin(s);
        float const sc = score(s);
        bool better = !found || bin > bestBin;
        if (!better && bin == bestBin) {
            if (bin == 1) {
                // Later death is better.
                better = s.deathTick > bestDeathTick ||
                         (s.deathTick == bestDeathTick && sc > bestScore + 1e-4f) ||
                         (s.deathTick == bestDeathTick && std::fabs(sc - bestScore) <= 1e-4f &&
                          s.delay < bestDelay);
            } else {
                better = sc > bestScore + 1e-4f ||
                         (std::fabs(sc - bestScore) <= 1e-4f && s.delay < bestDelay);
            }
        }
        if (!better)
            continue;
        found = true;
        bestDelay = s.delay;
        bestBin = bin;
        bestScore = sc;
        bestDeathTick = s.deathTick;
    }
    if (found) {
        r.hasBestEvaluated = true;
        r.bestEvaluatedDelay = bestDelay;
    }

    fillIdealGate(r, meta, Kind::PlannerMirror, false);
    applyClaimsGate(r, meta);
    return r;
}

EbDevObjective::Result EbDevObjective::evaluateHorizonClear(std::vector<Sample> const& samples,
                                                        Meta const& meta) {
    Result r;
    r.kind = Kind::HorizonClear;
    r.objectiveId = kindId(Kind::HorizonClear);
    r.horizonTicks = meta.horizonTicks;
    r.tailTicks = meta.tailTicks;
    r.objectivePartial = meta.objectivePartial;

    auto surv = bestSurvivingDelay(samples);
    if (surv) {
        r.hasBestSurviving = true;
        r.bestSurvivingDelay = *surv;
    }

    // Clear-eligible: max tailEndX, then min delay.
    bool foundClear = false;
    int bestClear = -1;
    float bestTailX = -std::numeric_limits<float>::infinity();
    for (auto const& s : samples) {
        if (!clearEligible(s))
            continue;
        if (!foundClear || s.tailEndX > bestTailX + 1e-4f ||
            (std::fabs(s.tailEndX - bestTailX) <= 1e-4f && s.delay < bestClear)) {
            foundClear = true;
            bestClear = s.delay;
            bestTailX = s.tailEndX;
        }
    }
    if (foundClear) {
        r.hasBestEvaluated = true;
        r.bestEvaluatedDelay = bestClear;
    }

    fillIdealGate(r, meta, Kind::HorizonClear, foundClear);
    if (r.idealEligible && foundClear) {
        r.hasIdealDelay = true;
        r.idealDelay = bestClear;
        r.idealBlockedReason.clear();
    }

    applyClaimsGate(r, meta);
    // applyClaimsGate clears ideal; restore blocked reason if suppressed.
    if (!meta.claimsAllowed && r.idealBlockedReason.empty()) {
        r.idealBlockedReason = "claims_suppressed";
    }
    return r;
}

EbDevObjective::Result EbDevObjective::evaluate(Kind kind, std::vector<Sample> const& samples,
                                            Meta const& meta) {
    switch (kind) {
    case Kind::PlannerMirror:
        return evaluatePlannerMirror(samples, meta);
    case Kind::HorizonClear:
        return evaluateHorizonClear(samples, meta);
    case Kind::SurviveThenEndX:
        break;
    }
    return evaluateSurviveThenEndX(samples, meta);
}

bool EbDevObjective::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += name;
    };

    expect(std::strcmp(kindId(Kind::SurviveThenEndX), "survive_then_endx_v1") == 0, "id-survive");
    expect(std::strcmp(kindId(Kind::HorizonClear), "horizon_clear_v1") == 0, "id-clear");

    // Fixture: two primary survivors; delay 5 has higher endX but tailFailed.
    std::vector<Sample> samples;
    {
        Sample a;
        a.delay = 2;
        a.survivedPrimary = true;
        a.endX = 100.f;
        a.tailFailed = false;
        a.tailComplete = true;
        a.tailEndX = 110.f;
        a.threatCleared = true;
        samples.push_back(a);

        Sample b;
        b.delay = 5;
        b.survivedPrimary = true;
        b.endX = 120.f;
        b.tailFailed = true;
        b.tailDeathTick = 60;
        b.tailComplete = true;
        b.tailEndX = 105.f;
        b.threatCleared = true;
        samples.push_back(b);

        Sample dead;
        dead.delay = 0;
        dead.survivedPrimary = false;
        dead.deathTick = 3;
        dead.endX = 40.f;
        samples.push_back(dead);
    }

    auto surv = bestSurvivingDelay(samples);
    expect(surv && *surv == 5, "best-surv-endx"); // higher endX wins (ignores tail)

    Meta meta;
    meta.claimsAllowed = true;
    meta.fidelity = DevFidelity::PlayerOnlyApproximated;
    meta.clockOk = true;
    meta.gridFull = false;
    meta.tailsComplete = true;
    meta.horizonTicks = 48;
    meta.tailTicks = 48;
    meta.objectivePartial = true;
    meta.latencyWeight = 0.25f;

    {
        auto result = evaluateSurviveThenEndX(samples, meta);
        expect(result.hasBestSurviving && result.bestSurvivingDelay == 5, "stx-surv");
        expect(result.hasBestEvaluated && result.bestEvaluatedDelay == 5, "stx-eval");
        expect(!result.idealEligible && !result.hasIdealDelay, "stx-no-ideal");
        expect(result.idealBlockedReason == std::string("objective_not_horizon_clear"),
               "stx-block");
    }

    {
        auto result = evaluateHorizonClear(samples, meta);
        // Only delay 2 is clear-eligible (5 has tailFailed).
        expect(result.hasBestEvaluated && result.bestEvaluatedDelay == 2, "hc-eval");
        expect(result.hasBestSurviving && result.bestSurvivingDelay == 5, "hc-surv");
        expect(!result.idealEligible, "hc-no-ideal-approx");
        expect(result.idealBlockedReason == std::string("fidelity"), "hc-block-fid");
    }

    // Ideal eligible only with reproduced + full grid + tails + clear set.
    {
        Meta m = meta;
        m.fidelity = DevFidelity::Reproduced;
        m.gridFull = true;
        m.objectivePartial = false;
        m.tailsComplete = true;
        m.claimsAllowed = true;
        m.clockOk = true;
        auto result = evaluateHorizonClear(samples, m);
        expect(result.idealEligible, "ideal-eligible");
        expect(result.hasIdealDelay && result.idealDelay == 2, "ideal-delay");
        expect(result.idealBlockedReason.empty(), "ideal-no-block");
    }

    // claims suppressed clears fields.
    {
        Meta m = meta;
        m.claimsAllowed = false;
        m.fidelity = DevFidelity::NotReproducible;
        auto result = evaluateSurviveThenEndX(samples, m);
        expect(!result.hasBestSurviving && !result.hasBestEvaluated, "suppress-fields");
        expect(result.idealBlockedReason == std::string("claims_suppressed"), "suppress-reason");
    }

    // classifyPrimaryAndTail
    {
        Sample s;
        classifyPrimaryAndTail(s, /*delay*/ 2, /*H*/ 4, /*T*/ 4,
                               /*died*/ true, /*deathTick*/ 7, /*endX*/ 55.f, false, false, false,
                               /*ticksRan*/ 8);
        // primaryEnd = 6; death at 7 → survived primary, tail fail
        expect(s.survivedPrimary && s.tailFailed, "tail-class-fail");
        expect(s.tailDeathTick == 7, "tail-death");

        Sample s2;
        classifyPrimaryAndTail(s2, 2, 4, 4, false, -1, 80.f, false, false, false, 10);
        expect(s2.survivedPrimary && !s2.tailFailed && s2.tailComplete, "tail-ok");
        expect(s2.tailEndX == 80.f, "tail-endx");

        Sample s3;
        classifyPrimaryAndTail(s3, 2, 4, 4, true, 3, 20.f, false, false, false, 4);
        expect(!s3.survivedPrimary, "primary-death");
    }

    // planner_mirror: survivor beats dead
    {
        auto result = evaluatePlannerMirror(samples, meta);
        expect(result.hasBestEvaluated, "pm-eval");
        // Both 2 and 5 survived — higher endX - λd: 120-1.25=118.75 vs 100-0.5=99.5 → 5
        expect(result.bestEvaluatedDelay == 5, "pm-pick");
        expect(!result.idealEligible, "pm-no-ideal");
    }

    // inconclusive ignored
    {
        std::vector<Sample> onlyBad;
        Sample u;
        u.delay = 1;
        u.unknown = true;
        u.survivedPrimary = true;
        u.endX = 999.f;
        onlyBad.push_back(u);
        auto result = evaluateSurviveThenEndX(onlyBad, meta);
        expect(!result.hasBestEvaluated && !result.hasBestSurviving, "inconclusive-skip");
    }

    if (fails == 0) {
        report = "DevObjective selfCheck OK";
        return true;
    }
    report = std::string("DevObjective selfCheck fails: ") + report;
    return false;
}
