#include "AssistPickV1.hpp"

#include <algorithm>

bool AssistPickV1::neighborRobust(
    CandView const* cands,
    int limit,
    int robustK,
    int delay,
    bool needActivation
) {
    if (!cands || delay < 0 || delay > limit) return false;
    auto const& c = cands[static_cast<size_t>(delay)];
    if (!c.viable) return false;
    if (needActivation && !c.activatedRing) return false;
    int const k = std::max(0, robustK);
    for (int e = delay - k; e <= delay + k; ++e) {
        if (e < 0 || e > limit) continue;
        auto const& n = cands[static_cast<size_t>(e)];
        if (!n.viable) return false;
        if (needActivation && !n.activatedRing) return false;
    }
    return true;
}

int AssistPickV1::pickEarliestRobust(
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
        if (!c.viable) continue;
        if (needActivation && !c.activatedRing) continue;
        return d;
    }
    return -1;
}

int AssistPickV1::pickLatestRobust(
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
        if (!c.viable) continue;
        if (needActivation && !c.activatedRing) continue;
        return d;
    }
    return -1;
}

AssistPickV1::Result AssistPickV1::choose(
    CandView const* cands,
    int limit,
    int robustK,
    bool orbIntent,
    int actMin,
    int actMax
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
        if (chosen >= 0) {
            out.chosen = chosen;
            out.requireOrb = true;
            return out;
        }
    }

    if (cands[0].viable) {
        out.chosen = 0;
        out.doPass = true;
        return out;
    }

    std::vector<int> pool;
    pool.reserve(static_cast<size_t>(limit) + 1);
    for (int d = 0; d <= limit; ++d) pool.push_back(d);
    out.chosen = pickLatestRobust(cands, limit, robustK, pool, false);
    return out;
}

bool AssistPickV1::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    // d0 viable → Pass
    {
        CandView c[] = { { true, false }, { true, false }, { true, false } };
        auto const r = choose(c, 2, 0, false, -1, -1);
        expect(r.doPass && r.chosen == 0 && !r.requireOrb, "d0-pass");
    }
    // d0 dies, latest viable = 2 (k=0)
    {
        CandView c[] = { { false, false }, { true, false }, { true, false } };
        auto const r = choose(c, 2, 0, false, -1, -1);
        expect(!r.doPass && r.chosen == 2, "latest-miss");
    }
    // k=1: only delay 1 is neighbor-robust among 0=dead,1=ok,2=ok → latest robust = 1
    // (2 has neighbor 1,2 ok; 1 has 0,1,2 — 0 dead → not robust; fallback last viable = 2)
    {
        CandView c[] = { { false, false }, { true, false }, { true, false } };
        auto const r = choose(c, 2, 1, false, -1, -1);
        // pickLatestRobust: d=2 neighbors 1..2 ok → best=2; d=1 needs 0 viable → skip
        expect(!r.doPass && r.chosen == 2, "latest-k1");
    }
    // Orb band: earliest activation
    {
        CandView c[] = {
            { false, false },
            { true, true },
            { true, true },
            { true, true }
        };
        auto const r = choose(c, 3, 0, true, 1, 3);
        expect(!r.doPass && r.chosen == 1 && r.requireOrb, "orb-earliest");
    }
    // pickEarliest / pickLatest helpers
    {
        CandView c[] = { { true, false }, { true, false }, { true, false } };
        std::vector<int> pool = { 0, 1, 2 };
        expect(pickEarliestRobust(c, 2, 0, pool, false) == 0, "earliest0");
        expect(pickLatestRobust(c, 2, 0, pool, false) == 2, "latest2");
    }
    // empty / hopeless
    {
        CandView c[] = { { false, false }, { false, false } };
        auto const r = choose(c, 1, 0, false, -1, -1);
        expect(r.chosen < 0 && !r.doPass, "hopeless");
    }

    if (fails == 0) {
        report = "AssistPickV1 selfCheck OK";
        return true;
    }
    report = std::string("AssistPickV1 ") + std::to_string(fails) + " fails: " + report;
    return false;
}
