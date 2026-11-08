#include "assist_pick_v2.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

bool AssistPickV2::isDelayWorthy(CandView const& c) {
    return c.viable && !c.died;
}

bool AssistPickV2::neighborRobust(CandView const* cands, int limit, int robustK, int delay,
                                  bool needActivation) {
    if (!cands || delay < 0 || delay > limit)
        return false;
    auto const& c = cands[static_cast<size_t>(delay)];
    if (!isDelayWorthy(c))
        return false;
    if (needActivation && !c.activatedRing)
        return false;
    int const offset = std::max(0, robustK);
    for (int entry = delay - offset; entry <= delay + offset; ++entry) {
        if (entry < 0 || entry > limit)
            continue;
        auto const& n = cands[static_cast<size_t>(entry)];
        if (!isDelayWorthy(n))
            return false;
        if (needActivation && !n.activatedRing)
            return false;
    }
    return true;
}

bool AssistPickV2::foresightBetter(CandView const& a, int delayA, CandView const& b, int delayB,
                                   float latencyWeight) {
    if (a.orbUseful != b.orbUseful)
        return a.orbUseful;
    if (a.surviveBin != b.surviveBin)
        return a.surviveBin > b.surviveBin;
    if (a.surviveTicks != b.surviveTicks) {
        return a.surviveTicks > b.surviveTicks;
    }
    float const lam = std::max(0.f, latencyWeight);
    float const scoreA = a.endX - lam * static_cast<float>(delayA);
    float const scoreB = b.endX - lam * static_cast<float>(delayB);
    if (!(std::fabs(scoreA - scoreB) > 0.5f)) {
        return delayA < delayB;
    }
    return scoreA > scoreB;
}

int AssistPickV2::pickEarliestRobust(CandView const* cands, int limit, int robustK,
                                     std::vector<int> const& pool, bool needActivation) {
    if (!cands || pool.empty() || limit < 0)
        return -1;
    int best = -1;
    for (int delay : pool) {
        if (!neighborRobust(cands, limit, robustK, delay, needActivation))
            continue;
        if (best < 0 || delay < best)
            best = delay;
    }
    if (best >= 0)
        return best;
    for (int delay : pool) {
        if (delay < 0 || delay > limit)
            continue;
        auto const& c = cands[static_cast<size_t>(delay)];
        if (!isDelayWorthy(c))
            continue;
        if (needActivation && !c.activatedRing)
            continue;
        return delay;
    }
    return -1;
}

int AssistPickV2::pickLatestRobust(CandView const* cands, int limit, int robustK,
                                   std::vector<int> const& pool, bool needActivation) {
    if (!cands || pool.empty() || limit < 0)
        return -1;
    int best = -1;
    for (int delay : pool) {
        if (!neighborRobust(cands, limit, robustK, delay, needActivation))
            continue;
        if (best < 0 || delay > best)
            best = delay;
    }
    if (best >= 0)
        return best;
    for (int index = static_cast<int>(pool.size()) - 1; index >= 0; --index) {
        int const delay = pool[static_cast<size_t>(index)];
        if (delay < 0 || delay > limit)
            continue;
        auto const& c = cands[static_cast<size_t>(delay)];
        if (!isDelayWorthy(c))
            continue;
        if (needActivation && !c.activatedRing)
            continue;
        return delay;
    }
    return -1;
}

int AssistPickV2::pickBestForesight(CandView const* cands, int limit, int robustK,
                                    std::vector<int> const& pool, bool needActivation,
                                    float latencyWeight) {
    if (!cands || pool.empty() || limit < 0)
        return -1;

    int bestRobust = -1;
    for (int delay : pool) {
        if (!neighborRobust(cands, limit, robustK, delay, needActivation))
            continue;
        if (bestRobust < 0 ||
            foresightBetter(cands[static_cast<size_t>(delay)], delay,
                            cands[static_cast<size_t>(bestRobust)], bestRobust, latencyWeight)) {
            bestRobust = delay;
        }
    }
    if (bestRobust >= 0)
        return bestRobust;

    int bestAny = -1;
    for (int delay : pool) {
        if (delay < 0 || delay > limit)
            continue;
        auto const& c = cands[static_cast<size_t>(delay)];
        if (!isDelayWorthy(c))
            continue;
        if (needActivation && !c.activatedRing)
            continue;
        if (bestAny < 0 || foresightBetter(c, delay, cands[static_cast<size_t>(bestAny)], bestAny,
                                           latencyWeight)) {
            bestAny = delay;
        }
    }
    return bestAny;
}

AssistPickV2::Result AssistPickV2::choose(CandView const* cands, int limit, int robustK,
                                          bool orbIntent, int actMin, int actMax,
                                          Context const& ctx) {
    Result output{};
    if (!cands || limit < 0)
        return output;

    bool const haveAct = actMin >= 0 && actMax >= actMin && actMin <= limit && actMax <= limit;

    if (orbIntent && haveAct) {
        std::vector<int> pool;
        pool.reserve(static_cast<size_t>(actMax - actMin + 1));
        for (int delay = actMin; delay <= actMax; ++delay)
            pool.push_back(delay);
        int const offset = std::max(0, robustK);
        int chosen = -1;
        if (actMax - actMin >= 2 * offset) {
            std::vector<int> interior;
            for (int delay = actMin + offset; delay <= actMax - offset; ++delay)
                interior.push_back(delay);
            chosen = pickEarliestRobust(cands, limit, robustK, interior, true);
        }
        if (chosen < 0) {
            chosen = pickEarliestRobust(cands, limit, robustK, pool, true);
        }
        if (chosen >= 0 && isDelayWorthy(cands[static_cast<size_t>(chosen)])) {
            output.chosen = chosen;
            output.requireOrb = true;
            return output;
        }
    }

    float const lam = ctx.latencyWeight;
    bool const forceSearch = ctx.idleSafer || ctx.longForesightNeedSearch || ctx.ascentUseful;

    std::vector<int> all;
    all.reserve(static_cast<size_t>(limit) + 1u);
    for (int delay = 0; delay <= limit; ++delay)
        all.push_back(delay);

    int chosen = pickBestForesight(cands, limit, robustK, all, false, lam);
    if (chosen >= 0 && !isDelayWorthy(cands[static_cast<size_t>(chosen)])) {
        chosen = -1;
    }

    // Force-search: prefer a later Alive Delay when one exists.
    // No Alive later → hopeless (chosen=-1), never soft-dead Delay.
    if (forceSearch) {
        std::vector<int> later;
        for (int delay = 1; delay <= limit; ++delay)
            later.push_back(delay);
        int const laterBest = pickBestForesight(cands, limit, robustK, later, false, lam);
        bool const laterOk = laterBest > 0 && isDelayWorthy(cands[static_cast<size_t>(laterBest)]);

        if (laterOk) {
            bool takeLater = false;
            if (chosen < 0 || chosen == 0) {
                if (chosen < 0 || !isDelayWorthy(cands[0])) {
                    takeLater = true;
                } else if (ctx.idleSafer) {
                    takeLater = true;
                } else if (foresightBetter(cands[static_cast<size_t>(laterBest)], laterBest,
                                           cands[0], 0, lam)) {
                    takeLater = true;
                } else if (ctx.longForesightNeedSearch &&
                           cands[static_cast<size_t>(laterBest)].surviveBin >=
                               cands[0].surviveBin) {
                    takeLater = true;
                }
            } else if (foresightBetter(cands[static_cast<size_t>(laterBest)], laterBest,
                                       cands[static_cast<size_t>(chosen)], chosen, lam)) {
                takeLater = true;
            }
            if (takeLater)
                chosen = laterBest;
        } else {
            // Flags wanted Delay, but no Alive later escape.
            if (chosen > 0)
                chosen = -1;
            if (chosen == 0 && !isDelayWorthy(cands[0]))
                chosen = -1;
            if (chosen < 0) {
                // d0 also not Delay-worthy → hopeless (both-dead / soft-dead).
                return output;
            }
            // d0 Alive → fall through to Pass.
        }
    }

    if (chosen < 0)
        return output;
    if (!isDelayWorthy(cands[static_cast<size_t>(chosen)])) {
        return output; // never arm Delay / Pass on dead soft
    }

    output.chosen = chosen;
    if (chosen == 0) {
        // idleSafer with a viable later already handled above; Pass only if
        // still on d0 Alive.
        output.doPass = true;
    }
    return output;
}

bool AssistPickV2::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += "; ";
        report += name;
    };

    expect(isDelayWorthy(CandView{true, false, false, 1, 30, 100.f, false}), "worthy-alive");
    expect(!isDelayWorthy(CandView{true, false, false, 2, 60, 150.f, true}),
           "worthy-not-soft-died");
    expect(!isDelayWorthy(CandView{false, false, false, 0, 5, 40.f, true}), "worthy-not-dead");

    // d0 viable Alive, no foresight flags → Pass
    {
        CandView candidate[] = {{true, false, false, 1, 30, 100.f, false},
                                {true, false, false, 1, 30, 110.f, false},
                                {true, false, false, 1, 30, 120.f, false}};
        auto const result = choose(candidate, 2, 0, false, -1, -1);
        expect(result.doPass && result.chosen == 0, "d0-pass");
    }

    // Foresight rank: higher surviveBin wins over "latest"
    {
        CandView candidate[] = {{false, false, false, 0, 5, 50.f, true},
                                {true, false, false, 3, 100, 200.f, false},
                                {true, false, false, 1, 40, 300.f, false}};
        std::vector<int> pool = {0, 1, 2};
        expect(pickBestForesight(candidate, 2, 0, pool, false, 0.f) == 1, "best-bin");
        auto const result = choose(candidate, 2, 0, false, -1, -1);
        expect(!result.doPass && result.chosen == 1, "choose-bin-not-latest");
    }

    // endX − λ·delay: earlier with similar endX wins when λ high
    {
        CandView firstValue{true, false, false, 2, 50, 100.f, false};
        CandView secondValue{true, false, false, 2, 50, 105.f, false};
        expect(foresightBetter(firstValue, 1, secondValue, 10, 1.f), "latency-earlier");
        expect(!foresightBetter(firstValue, 1, secondValue, 2, 0.f), "no-lam-farther");
    }

    // idleSafer → Delay later even if d0 Alive viable
    {
        CandView candidate[] = {{true, false, false, 1, 30, 100.f, false},
                                {true, false, false, 2, 60, 150.f, false},
                                {true, false, false, 2, 60, 160.f, false}};
        Context ctx;
        ctx.idleSafer = true;
        ctx.ascentUseful = true;
        auto const result = choose(candidate, 2, 0, false, -1, -1, ctx);
        expect(!result.doPass && result.chosen >= 1, "idle-force-delay");
    }

    // longForesightNeedSearch + later same bin → prefer later Delay
    {
        CandView candidate[] = {{true, false, false, 1, 40, 100.f, false},
                                {true, false, false, 1, 45, 120.f, false},
                                {true, false, false, 1, 50, 140.f, false}};
        Context ctx;
        ctx.longForesightNeedSearch = true;
        auto const result = choose(candidate, 2, 0, false, -1, -1, ctx);
        expect(!result.doPass && result.chosen >= 1, "lf-need-search");
    }

    // Both dead + force flags → hopeless, no Delay
    {
        CandView candidate[] = {{false, false, false, 0, 10, 50.f, true},
                                {false, false, false, 2, 80, 150.f, true},
                                {false, false, false, 3, 100, 200.f, true}};
        Context ctx;
        ctx.idleSafer = true;
        ctx.ascentUseful = true;
        ctx.longForesightNeedSearch = true;
        auto const result = choose(candidate, 2, 0, false, -1, -1, ctx);
        expect(result.chosen < 0 && !result.doPass, "both-dead-no-delay");
    }

    // Soft-viable but died must never arm Delay under force-search
    {
        CandView candidate[] = {{false, false, false, 0, 5, 40.f, true},
                                {true, false, false, 2, 60, 120.f, true},
                                {true, false, false, 3, 90, 180.f, true}};
        Context ctx;
        ctx.idleSafer = true;
        ctx.ascentUseful = true;
        auto const result = choose(candidate, 2, 0, false, -1, -1, ctx);
        expect(result.chosen < 0 && !result.doPass, "soft-died-no-delay");
    }

    // Coast/d0 dies, later Alive → may Delay
    {
        CandView candidate[] = {{false, false, false, 0, 8, 50.f, true},
                                {true, false, false, 2, 64, 180.f, false},
                                {true, false, false, 2, 70, 200.f, false}};
        Context ctx;
        ctx.idleSafer = true;
        ctx.longForesightNeedSearch = true;
        auto const result = choose(candidate, 2, 0, false, -1, -1, ctx);
        expect(!result.doPass && result.chosen >= 1, "coast-die-alive-later");
    }

    // Orb band: earliest activation (Alive)
    {
        CandView candidate[] = {{false, false, false, 0, 0, 0.f, true},
                                {true, true, true, 1, 30, 100.f, false},
                                {true, true, true, 2, 60, 200.f, false},
                                {true, true, true, 2, 60, 210.f, false}};
        auto const result = choose(candidate, 3, 0, true, 1, 3);
        expect(!result.doPass && result.chosen == 1 && result.requireOrb, "orb-earliest");
    }

    {
        CandView candidate[] = {{true, false, false, 1, 30, 100.f, false},
                                {true, false, false, 1, 30, 110.f, false},
                                {true, false, false, 1, 30, 120.f, false}};
        std::vector<int> pool = {0, 1, 2};
        expect(pickEarliestRobust(candidate, 2, 0, pool, false) == 0, "earliest0");
        expect(pickLatestRobust(candidate, 2, 0, pool, false) == 2, "latest2");
    }

    {
        CandView candidate[] = {{false, false, false, 0, 0, 0.f, true},
                                {false, false, false, 0, 0, 0.f, true}};
        auto const result = choose(candidate, 1, 0, false, -1, -1);
        expect(result.chosen < 0 && !result.doPass, "hopeless");
    }

    if (fails == 0) {
        report = "AssistPickV2 selfCheck OK";
        return true;
    }
    report = fmt::format("AssistPickV2 selfCheck {} fails: {}", fails, report);
    return false;
}
