#include "AssistPickV2.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

bool AssistPickV2::isDelayWorthy(CandView const& c) {
    return c.viable && !c.died;
}

bool AssistPickV2::neighborRobust(
    CandView const* cands,
    int limit,
    int robustK,
    int delay,
    bool needActivation
) {
    if (!cands || delay < 0 || delay > limit) return false;
    auto const& c = cands[static_cast<size_t>(delay)];
    if (!isDelayWorthy(c)) return false;
    if (needActivation && !c.activatedRing) return false;
    int const k = std::max(0, robustK);
    for (int e = delay - k; e <= delay + k; ++e) {
        if (e < 0 || e > limit) continue;
        auto const& n = cands[static_cast<size_t>(e)];
        if (!isDelayWorthy(n)) return false;
        if (needActivation && !n.activatedRing) return false;
    }
    return true;
}

bool AssistPickV2::foresightBetter(
    CandView const& a,
    int delayA,
    CandView const& b,
    int delayB,
    float latencyWeight
) {
    if (a.orbUseful != b.orbUseful) return a.orbUseful;
    if (a.surviveBin != b.surviveBin) return a.surviveBin > b.surviveBin;
    if (a.surviveTicks != b.surviveTicks) {
        return a.surviveTicks > b.surviveTicks;
    }
    float const lam = std::max(0.f, latencyWeight);
    float const scoreA = a.endX - lam * static_cast<float>(delayA);
    float const scoreB = b.endX - lam * static_cast<float>(delayB);
    if (std::fabs(scoreA - scoreB) > 0.5f) return scoreA > scoreB;
    // Tie → earlier delay (fire sooner when foresight equal).
    return delayA < delayB;
}

int AssistPickV2::pickEarliestRobust(
    CandView const* cands,
    int limit,
    int robustK,
    std::vector<int> const& pool,
    bool needActivation
) {
    if (!cands || pool.empty() || limit < 0) return -1;
    int best = -1;
    for (int d : pool) {
        if (!neighborRobust(cands, limit, robustK, d, needActivation)) continue;
        if (best < 0 || d < best) best = d;
    }
    if (best >= 0) return best;
    for (int d : pool) {
        if (d < 0 || d > limit) continue;
        auto const& c = cands[static_cast<size_t>(d)];
        if (!isDelayWorthy(c)) continue;
        if (needActivation && !c.activatedRing) continue;
        return d;
    }
    return -1;
}

int AssistPickV2::pickLatestRobust(
    CandView const* cands,
    int limit,
    int robustK,
    std::vector<int> const& pool,
    bool needActivation
) {
    if (!cands || pool.empty() || limit < 0) return -1;
    int best = -1;
    for (int d : pool) {
        if (!neighborRobust(cands, limit, robustK, d, needActivation)) continue;
        if (best < 0 || d > best) best = d;
    }
    if (best >= 0) return best;
    for (int i = static_cast<int>(pool.size()) - 1; i >= 0; --i) {
        int const d = pool[static_cast<size_t>(i)];
        if (d < 0 || d > limit) continue;
        auto const& c = cands[static_cast<size_t>(d)];
        if (!isDelayWorthy(c)) continue;
        if (needActivation && !c.activatedRing) continue;
        return d;
    }
    return -1;
}

int AssistPickV2::pickBestForesight(
    CandView const* cands,
    int limit,
    int robustK,
    std::vector<int> const& pool,
    bool needActivation,
    float latencyWeight
) {
    if (!cands || pool.empty() || limit < 0) return -1;

    int bestRobust = -1;
    for (int d : pool) {
        if (!neighborRobust(cands, limit, robustK, d, needActivation)) continue;
        if (bestRobust < 0
            || foresightBetter(
                cands[static_cast<size_t>(d)],
                d,
                cands[static_cast<size_t>(bestRobust)],
                bestRobust,
                latencyWeight
            )) {
            bestRobust = d;
        }
    }
    if (bestRobust >= 0) return bestRobust;

    int bestAny = -1;
    for (int d : pool) {
        if (d < 0 || d > limit) continue;
        auto const& c = cands[static_cast<size_t>(d)];
        if (!isDelayWorthy(c)) continue;
        if (needActivation && !c.activatedRing) continue;
        if (bestAny < 0
            || foresightBetter(
                c, d, cands[static_cast<size_t>(bestAny)], bestAny, latencyWeight
            )) {
            bestAny = d;
        }
    }
    return bestAny;
}

AssistPickV2::Result AssistPickV2::choose(
    CandView const* cands,
    int limit,
    int robustK,
    bool orbIntent,
    int actMin,
    int actMax,
    Context const& ctx
) {
    Result out{};
    if (!cands || limit < 0) return out;

    bool const haveAct = actMin >= 0 && actMax >= actMin
        && actMin <= limit && actMax <= limit;

    if (orbIntent && haveAct) {
        std::vector<int> pool;
        pool.reserve(static_cast<size_t>(actMax - actMin + 1));
        for (int d = actMin; d <= actMax; ++d) pool.push_back(d);
        int const k = std::max(0, robustK);
        int chosen = -1;
        if (actMax - actMin >= 2 * k) {
            std::vector<int> interior;
            for (int d = actMin + k; d <= actMax - k; ++d) interior.push_back(d);
            chosen = pickEarliestRobust(cands, limit, robustK, interior, true);
        }
        if (chosen < 0) {
            chosen = pickEarliestRobust(cands, limit, robustK, pool, true);
        }
        if (chosen >= 0 && isDelayWorthy(cands[static_cast<size_t>(chosen)])) {
            out.chosen = chosen;
            out.requireOrb = true;
            return out;
        }
    }

    float const lam = ctx.latencyWeight;
    bool const forceSearch =
        ctx.idleSafer || ctx.longForesightNeedSearch || ctx.ascentUseful;

    std::vector<int> all;
    all.reserve(static_cast<size_t>(limit) + 1u);
    for (int d = 0; d <= limit; ++d) all.push_back(d);

    int chosen = pickBestForesight(
        cands, limit, robustK, all, false, lam
    );
    if (chosen >= 0 && !isDelayWorthy(cands[static_cast<size_t>(chosen)])) {
        chosen = -1;
    }

    // Force-search: prefer a later Alive Delay when one exists.
    // No Alive later → hopeless (chosen=-1), never soft-dead Delay.
    if (forceSearch) {
        std::vector<int> later;
        for (int d = 1; d <= limit; ++d) later.push_back(d);
        int const laterBest = pickBestForesight(
            cands, limit, robustK, later, false, lam
        );
        bool const laterOk = laterBest > 0
            && isDelayWorthy(cands[static_cast<size_t>(laterBest)]);

        if (laterOk) {
            bool takeLater = false;
            if (chosen < 0 || chosen == 0) {
                if (chosen < 0 || !isDelayWorthy(cands[0])) {
                    takeLater = true;
                }
                else if (ctx.idleSafer) {
                    takeLater = true;
                }
                else if (foresightBetter(
                    cands[static_cast<size_t>(laterBest)],
                    laterBest,
                    cands[0],
                    0,
                    lam
                )) {
                    takeLater = true;
                }
                else if (ctx.longForesightNeedSearch
                    && cands[static_cast<size_t>(laterBest)].surviveBin
                        >= cands[0].surviveBin) {
                    takeLater = true;
                }
            }
            else if (foresightBetter(
                cands[static_cast<size_t>(laterBest)],
                laterBest,
                cands[static_cast<size_t>(chosen)],
                chosen,
                lam
            )) {
                takeLater = true;
            }
            if (takeLater) chosen = laterBest;
        }
        else {
            // Flags wanted Delay, but no Alive later escape.
            if (chosen > 0) chosen = -1;
            if (chosen == 0 && !isDelayWorthy(cands[0])) chosen = -1;
            if (chosen < 0) {
                // d0 also not Delay-worthy → hopeless (both-dead / soft-dead).
                return out;
            }
            // d0 Alive → fall through to Pass.
        }
    }

    if (chosen < 0) return out;
    if (!isDelayWorthy(cands[static_cast<size_t>(chosen)])) {
        return out; // never arm Delay / Pass on dead soft
    }

    out.chosen = chosen;
    if (chosen == 0) {
        // idleSafer with a viable later already handled above; Pass only if
        // still on d0 Alive.
        out.doPass = true;
    }
    return out;
}

bool AssistPickV2::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(isDelayWorthy(CandView{ true, false, false, 1, 30, 100.f, false }),
        "worthy-alive");
    expect(!isDelayWorthy(CandView{ true, false, false, 2, 60, 150.f, true }),
        "worthy-not-soft-died");
    expect(!isDelayWorthy(CandView{ false, false, false, 0, 5, 40.f, true }),
        "worthy-not-dead");

    // d0 viable Alive, no foresight flags → Pass
    {
        CandView c[] = {
            { true, false, false, 1, 30, 100.f, false },
            { true, false, false, 1, 30, 110.f, false },
            { true, false, false, 1, 30, 120.f, false }
        };
        auto const r = choose(c, 2, 0, false, -1, -1, Context{});
        expect(r.doPass && r.chosen == 0, "d0-pass");
    }

    // Foresight rank: higher surviveBin wins over "latest"
    {
        CandView c[] = {
            { false, false, false, 0, 5, 50.f, true },
            { true, false, false, 3, 100, 200.f, false },
            { true, false, false, 1, 40, 300.f, false }
        };
        std::vector<int> pool = { 0, 1, 2 };
        expect(pickBestForesight(c, 2, 0, pool, false, 0.f) == 1, "best-bin");
        auto const r = choose(c, 2, 0, false, -1, -1, Context{});
        expect(!r.doPass && r.chosen == 1, "choose-bin-not-latest");
    }

    // endX − λ·delay: earlier with similar endX wins when λ high
    {
        CandView a{ true, false, false, 2, 50, 100.f, false };
        CandView b{ true, false, false, 2, 50, 105.f, false };
        expect(foresightBetter(a, 1, b, 10, 1.f), "latency-earlier");
        expect(!foresightBetter(a, 1, b, 2, 0.f), "no-lam-farther");
    }

    // idleSafer → Delay later even if d0 Alive viable
    {
        CandView c[] = {
            { true, false, false, 1, 30, 100.f, false },
            { true, false, false, 2, 60, 150.f, false },
            { true, false, false, 2, 60, 160.f, false }
        };
        Context ctx;
        ctx.idleSafer = true;
        ctx.ascentUseful = true;
        auto const r = choose(c, 2, 0, false, -1, -1, ctx);
        expect(!r.doPass && r.chosen >= 1, "idle-force-delay");
    }

    // longForesightNeedSearch + later same bin → prefer later Delay
    {
        CandView c[] = {
            { true, false, false, 1, 40, 100.f, false },
            { true, false, false, 1, 45, 120.f, false },
            { true, false, false, 1, 50, 140.f, false }
        };
        Context ctx;
        ctx.longForesightNeedSearch = true;
        auto const r = choose(c, 2, 0, false, -1, -1, ctx);
        expect(!r.doPass && r.chosen >= 1, "lf-need-search");
    }

    // Both dead + force flags → hopeless, no Delay
    {
        CandView c[] = {
            { false, false, false, 0, 10, 50.f, true },
            { false, false, false, 2, 80, 150.f, true },
            { false, false, false, 3, 100, 200.f, true }
        };
        Context ctx;
        ctx.idleSafer = true;
        ctx.ascentUseful = true;
        ctx.longForesightNeedSearch = true;
        auto const r = choose(c, 2, 0, false, -1, -1, ctx);
        expect(r.chosen < 0 && !r.doPass, "both-dead-no-delay");
    }

    // Soft-viable but died must never arm Delay under force-search
    {
        CandView c[] = {
            { false, false, false, 0, 5, 40.f, true },
            { true, false, false, 2, 60, 120.f, true },
            { true, false, false, 3, 90, 180.f, true }
        };
        Context ctx;
        ctx.idleSafer = true;
        ctx.ascentUseful = true;
        auto const r = choose(c, 2, 0, false, -1, -1, ctx);
        expect(r.chosen < 0 && !r.doPass, "soft-died-no-delay");
    }

    // ascentUseful alone + soft-died later → hopeless (no invent Delay)
    {
        CandView c[] = {
            { false, false, false, 0, 5, 40.f, true },
            { true, false, false, 3, 80, 160.f, true },
            { true, false, false, 3, 90, 180.f, true }
        };
        Context ctx;
        ctx.ascentUseful = true;
        auto const r = choose(c, 2, 0, false, -1, -1, ctx);
        expect(r.chosen < 0 && !r.doPass, "ascent-alone-soft-no-delay");
    }

    // Coast/d0 dies, later Alive → may Delay
    {
        CandView c[] = {
            { false, false, false, 0, 8, 50.f, true },
            { true, false, false, 2, 64, 180.f, false },
            { true, false, false, 2, 70, 200.f, false }
        };
        Context ctx;
        ctx.idleSafer = true;
        ctx.longForesightNeedSearch = true;
        auto const r = choose(c, 2, 0, false, -1, -1, ctx);
        expect(!r.doPass && r.chosen >= 1, "coast-die-alive-later");
    }

    // Orb band: earliest activation (Alive)
    {
        CandView c[] = {
            { false, false, false, 0, 0, 0.f, true },
            { true, true, true, 1, 30, 100.f, false },
            { true, true, true, 2, 60, 200.f, false },
            { true, true, true, 2, 60, 210.f, false }
        };
        auto const r = choose(c, 3, 0, true, 1, 3, Context{});
        expect(!r.doPass && r.chosen == 1 && r.requireOrb, "orb-earliest");
    }

    {
        CandView c[] = {
            { true, false, false, 1, 30, 100.f, false },
            { true, false, false, 1, 30, 110.f, false },
            { true, false, false, 1, 30, 120.f, false }
        };
        std::vector<int> pool = { 0, 1, 2 };
        expect(pickEarliestRobust(c, 2, 0, pool, false) == 0, "earliest0");
        expect(pickLatestRobust(c, 2, 0, pool, false) == 2, "latest2");
    }

    {
        CandView c[] = {
            { false, false, false, 0, 0, 0.f, true },
            { false, false, false, 0, 0, 0.f, true }
        };
        auto const r = choose(c, 1, 0, false, -1, -1, Context{});
        expect(r.chosen < 0 && !r.doPass, "hopeless");
    }

    if (fails == 0) {
        report = "AssistPickV2 selfCheck OK";
        return true;
    }
    report = fmt::format("AssistPickV2 selfCheck {} fails: {}", fails, report);
    return false;
}
