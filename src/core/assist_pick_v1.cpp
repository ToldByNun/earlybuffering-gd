#include "assist_pick_v1.hpp"
#include <algorithm>

bool AssistPickV1::neighborRobust(CandView const* cands, int limit, int robustK, int delay,
                                  bool needActivation) {
    if (!cands || delay < 0 || delay > limit)
        return false;
    auto const& c = cands[static_cast<size_t>(delay)];
    if (!c.viable)
        return false;
    if (needActivation && !c.activatedRing)
        return false;
    int const offset = std::max(0, robustK);
    for (int entry = delay - offset; entry <= delay + offset; ++entry) {
        if (entry < 0 || entry > limit)
            continue;
        auto const& n = cands[static_cast<size_t>(entry)];
        if (!n.viable)
            return false;
        if (needActivation && !n.activatedRing)
            return false;
    }
    return true;
}

int AssistPickV1::pickEarliestRobust(CandView const* cands, int limit, int robustK,
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
        if (!c.viable)
            continue;
        if (needActivation && !c.activatedRing)
            continue;
        return delay;
    }
    return -1;
}

int AssistPickV1::pickLatestRobust(CandView const* cands, int limit, int robustK,
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
        if (!c.viable)
            continue;
        if (needActivation && !c.activatedRing)
            continue;
        return delay;
    }
    return -1;
}

AssistPickV1::Result AssistPickV1::choose(CandView const* cands, int limit, int robustK,
                                          bool orbIntent, int actMin, int actMax) {
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
        if (chosen >= 0) {
            output.chosen = chosen;
            output.requireOrb = true;
            return output;
        }
    }

    if (cands[0].viable) {
        output.chosen = 0;
        output.doPass = true;
        return output;
    }

    std::vector<int> pool;
    pool.reserve(static_cast<size_t>(limit) + 1);
    for (int delay = 0; delay <= limit; ++delay)
        pool.push_back(delay);
    output.chosen = pickLatestRobust(cands, limit, robustK, pool, false);
    return output;
}

bool AssistPickV1::selfCheck(std::string& report) {
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

    // d0 viable → Pass
    {
        CandView candidate[] = {{true, false}, {true, false}, {true, false}};
        auto const result = choose(candidate, 2, 0, false, -1, -1);
        expect(result.doPass && result.chosen == 0 && !result.requireOrb, "d0-pass");
    }
    // d0 dies, latest viable = 2 (k=0)
    {
        CandView candidate[] = {{false, false}, {true, false}, {true, false}};
        auto const result = choose(candidate, 2, 0, false, -1, -1);
        expect(!result.doPass && result.chosen == 2, "latest-miss");
    }
    // k=1: only delay 1 is neighbor-robust among 0=dead,1=ok,2=ok → latest robust = 1
    // (2 has neighbor 1,2 ok; 1 has 0,1,2 — 0 dead → not robust; fallback last viable = 2)
    {
        CandView candidate[] = {{false, false}, {true, false}, {true, false}};
        auto const result = choose(candidate, 2, 1, false, -1, -1);
        // pickLatestRobust: d=2 neighbors 1..2 ok → best=2; d=1 needs 0 viable → skip
        expect(!result.doPass && result.chosen == 2, "latest-k1");
    }
    // Orb band: earliest activation
    {
        CandView candidate[] = {{false, false}, {true, true}, {true, true}, {true, true}};
        auto const result = choose(candidate, 3, 0, true, 1, 3);
        expect(!result.doPass && result.chosen == 1 && result.requireOrb, "orb-earliest");
    }
    // pickEarliest / pickLatest helpers
    {
        CandView candidate[] = {{true, false}, {true, false}, {true, false}};
        std::vector<int> pool = {0, 1, 2};
        expect(pickEarliestRobust(candidate, 2, 0, pool, false) == 0, "earliest0");
        expect(pickLatestRobust(candidate, 2, 0, pool, false) == 2, "latest2");
    }
    // empty / hopeless
    {
        CandView candidate[] = {{false, false}, {false, false}};
        auto const result = choose(candidate, 1, 0, false, -1, -1);
        expect(result.chosen < 0 && !result.doPass, "hopeless");
    }

    if (fails == 0) {
        report = "AssistPickV1 selfCheck OK";
        return true;
    }
    report = std::string("AssistPickV1 ") + std::to_string(fails) + " fails: " + report;
    return false;
}
