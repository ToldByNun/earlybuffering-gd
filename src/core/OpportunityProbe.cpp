#include "OpportunityProbe.hpp"

#include <algorithm>
#include <cmath>

int OpportunityProbe::midDelay(int limit) {
    if (limit <= 0) return 0;
    int mid = limit / 2;
    if (mid < 1) mid = 1;
    if (mid > limit) mid = limit;
    return mid;
}

int OpportunityProbe::orbScanTicks(int limit, int slack) {
    int const lim = std::max(0, limit);
    int const sl = std::max(0, slack);
    return std::max(lim + sl + 4, lim + 1);
}

int OpportunityProbe::pressReachScanTicks(int limit, int slack) {
    int const base = OpportunityProbe::orbScanTicks(limit, slack);
    return std::min(base, OpportunityProbe::kMaxPressReachExtraTicks);
}

bool OpportunityProbe::sampleShowsOpportunity(
    Sample const& baseline, Sample const& probe
) {
    if (probe.diedBeforeAction) return false;
    if (probe.activatedRing && !baseline.activatedRing) return true;
    if (probe.surviveBin > baseline.surviveBin) return true;
    float const gain = probe.endX - baseline.endX;
    if (gain >= OpportunityProbe::kEndXGain) return true;
    return false;
}

bool OpportunityProbe::shouldSearchFurther(
    bool foresightOrbPad,
    bool orbWindowFound,
    bool d0ActivatedRing,
    Sample const& baseline,
    Sample const* probes,
    int probeCount
) {
    if (foresightOrbPad) return true;
    if (orbWindowFound) return true;
    if (d0ActivatedRing) return true;
    if (!probes || probeCount <= 0) return false;
    for (int i = 0; i < probeCount; ++i) {
        if (OpportunityProbe::sampleShowsOpportunity(baseline, probes[i])) {
            return true;
        }
    }
    return false;
}

bool OpportunityProbe::allowPassNoThreat(
    bool foresightOrbPad,
    bool orbWindowFound,
    bool d0ActivatedRing,
    Sample const& baseline,
    Sample const* probes,
    int probeCount
) {
    return !OpportunityProbe::shouldSearchFurther(
        foresightOrbPad,
        orbWindowFound,
        d0ActivatedRing,
        baseline,
        probes,
        probeCount
    );
}

bool OpportunityProbe::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += ", ";
        report += label;
    };

    expect(OpportunityProbe::midDelay(16) == 8, "mid-16");
    expect(OpportunityProbe::midDelay(1) == 1, "mid-1");
    expect(OpportunityProbe::midDelay(0) == 0, "mid-0");
    expect(OpportunityProbe::midDelay(5) == 2, "mid-5");

    expect(OpportunityProbe::orbScanTicks(16, 2) == 22, "scan-16-2");
    expect(OpportunityProbe::orbScanTicks(2, 0) == 6, "scan-2-0");
    expect(OpportunityProbe::orbScanTicks(0, 0) == 1, "scan-0");
    expect(
        OpportunityProbe::pressReachScanTicks(64, 8)
            == OpportunityProbe::kMaxPressReachExtraTicks,
        "press-reach-cap"
    );

    Sample base{};
    base.surviveBin = 0;
    base.endX = 100.f;

    Sample betterEnd = base;
    betterEnd.endX = 100.f + OpportunityProbe::kEndXGain;
    expect(
        OpportunityProbe::sampleShowsOpportunity(base, betterEnd),
        "opp-endx"
    );

    Sample tinyEnd = base;
    tinyEnd.endX = 100.f + 3.f;
    expect(
        !OpportunityProbe::sampleShowsOpportunity(base, tinyEnd),
        "opp-endx-small"
    );

    Sample betterBin = base;
    betterBin.surviveBin = 1;
    expect(
        OpportunityProbe::sampleShowsOpportunity(base, betterBin),
        "opp-bin"
    );

    Sample act = base;
    act.activatedRing = true;
    expect(
        OpportunityProbe::sampleShowsOpportunity(base, act),
        "opp-ring"
    );

    Sample died = act;
    died.diedBeforeAction = true;
    expect(
        !OpportunityProbe::sampleShowsOpportunity(base, died),
        "opp-died-before"
    );

    // G2: foresight hit + d0 alive → must search (never PassNoThreat).
    expect(
        OpportunityProbe::shouldSearchFurther(
            true, false, false, base, nullptr, 0
        ),
        "g2-foresight-search"
    );
    expect(
        !OpportunityProbe::allowPassNoThreat(
            true, false, false, base, nullptr, 0
        ),
        "g2-foresight-no-pass"
    );

    expect(
        OpportunityProbe::shouldSearchFurther(
            false, true, false, base, nullptr, 0
        ),
        "g2-orbwin-search"
    );
    expect(
        OpportunityProbe::shouldSearchFurther(
            false, false, true, base, nullptr, 0
        ),
        "g2-d0-ring-search"
    );

    Sample probes[2];
    probes[0] = betterEnd;
    expect(
        OpportunityProbe::shouldSearchFurther(
            false, false, false, base, probes, 1
        ),
        "g2-probe-search"
    );
    expect(
        !OpportunityProbe::allowPassNoThreat(
            false, false, false, base, probes, 1
        ),
        "g2-probe-no-pass"
    );

    Sample emptyProbe = base;
    probes[0] = emptyProbe;
    expect(
        !OpportunityProbe::shouldSearchFurther(
            false, false, false, base, probes, 1
        ),
        "g2-empty-no-search"
    );
    expect(
        OpportunityProbe::allowPassNoThreat(
            false, false, false, base, probes, 1
        ),
        "g2-empty-allow-pass"
    );
    expect(
        OpportunityProbe::allowPassNoThreat(
            false, false, false, base, nullptr, 0
        ),
        "g2-no-probes-allow-pass"
    );

    if (fails == 0) {
        report = "OpportunityProbe selfCheck OK";
        return true;
    }
    report = std::string("OpportunityProbe selfCheck fails: ") + report;
    return false;
}
