#include "AssistPickV2.hpp"
#include "AssistPickV1.hpp"

#include <fmt/format.h>
#include <vector>

namespace {

AssistPickV1::CandView toV1(AssistPickV2::CandView const& c) {
    AssistPickV1::CandView v;
    v.viable = c.viable;
    v.activatedRing = c.activatedRing;
    return v;
}

std::vector<AssistPickV1::CandView> toV1Views(
    AssistPickV2::CandView const* cands, int limit
) {
    std::vector<AssistPickV1::CandView> out;
    if (!cands || limit < 0) return out;
    out.resize(static_cast<size_t>(limit) + 1u);
    for (int d = 0; d <= limit; ++d) {
        out[static_cast<size_t>(d)] = toV1(cands[static_cast<size_t>(d)]);
    }
    return out;
}

} // namespace

int AssistPickV2::pickEarliestRobust(
    CandView const* cands,
    int limit,
    int robustK,
    std::vector<int> const& pool,
    bool needActivation
) {
    auto views = toV1Views(cands, limit);
    if (views.empty()) return -1;
    return AssistPickV1::pickEarliestRobust(
        views.data(), limit, robustK, pool, needActivation
    );
}

int AssistPickV2::pickLatestRobust(
    CandView const* cands,
    int limit,
    int robustK,
    std::vector<int> const& pool,
    bool needActivation
) {
    auto views = toV1Views(cands, limit);
    if (views.empty()) return -1;
    return AssistPickV1::pickLatestRobust(
        views.data(), limit, robustK, pool, needActivation
    );
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
    (void)ctx; // WU11: idleSafer / ascent / long foresight steer
    Result out{};
    auto views = toV1Views(cands, limit);
    if (views.empty()) return out;
    auto const r = AssistPickV1::choose(
        views.data(), limit, robustK, orbIntent, actMin, actMax
    );
    out.chosen = r.chosen;
    out.doPass = r.doPass;
    out.requireOrb = r.requireOrb;
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

    // Parity with V1 fixtures (skeleton must match frozen v1).
    {
        CandView c[] = { { true, false }, { true, false }, { true, false } };
        auto const r = choose(c, 2, 0, false, -1, -1);
        expect(r.doPass && r.chosen == 0 && !r.requireOrb, "d0-pass");
        AssistPickV1::CandView v1[] = {
            { true, false }, { true, false }, { true, false }
        };
        auto const r1 = AssistPickV1::choose(v1, 2, 0, false, -1, -1);
        expect(r.chosen == r1.chosen && r.doPass == r1.doPass, "parity-d0");
    }
    {
        CandView c[] = { { false, false }, { true, false }, { true, false } };
        auto const r = choose(c, 2, 0, false, -1, -1);
        expect(!r.doPass && r.chosen == 2, "latest-miss");
    }
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
    {
        CandView c[] = { { true, false }, { true, false }, { true, false } };
        std::vector<int> pool = { 0, 1, 2 };
        expect(pickEarliestRobust(c, 2, 0, pool, false) == 0, "earliest0");
        expect(pickLatestRobust(c, 2, 0, pool, false) == 2, "latest2");
    }
    // Context is accepted but unused (skeleton compile/API hook).
    {
        CandView c[] = { { false, false }, { false, false } };
        Context ctx;
        ctx.idleSafer = true;
        ctx.longForesightNeedSearch = true;
        auto const r = choose(c, 1, 0, false, -1, -1, ctx);
        expect(r.chosen < 0 && !r.doPass, "hopeless-ctx");
    }
    // Reserved fields must not change skeleton outcome.
    {
        CandView c[] = {
            { false, false, 0, 0.f },
            { true, false, 3, 999.f },
            { true, false, 1, 10.f }
        };
        auto const r = choose(c, 2, 0, false, -1, -1);
        expect(!r.doPass && r.chosen == 2, "ignore-survive-endx");
    }

    if (fails == 0) {
        report = "AssistPickV2 selfCheck OK";
        return true;
    }
    report = fmt::format("AssistPickV2 selfCheck {} fails: {}", fails, report);
    return false;
}
