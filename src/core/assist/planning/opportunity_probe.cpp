#include "opportunity_probe.hpp"
#include <algorithm>
#include <cmath>

int EbOpportunityProbe::midDelay(int limit) {
    if (limit <= 0)
        return 0;
    int mid = limit / 2;
    if (mid < 1)
        mid = 1;
    if (mid > limit)
        mid = limit;
    return mid;
}

int EbOpportunityProbe::orbScanTicks(int limit, int slack) {
    int const limitValue = std::max(0, limit);
    int const sl = std::max(0, slack);
    return std::max(limitValue + sl + 4, limitValue + 1);
}

int EbOpportunityProbe::pressReachScanTicks(int limit, int slack) {
    int const base = EbOpportunityProbe::orbScanTicks(limit, slack);
    return std::min(base, EbOpportunityProbe::kMaxPressReachExtraTicks);
}

bool EbOpportunityProbe::sampleShowsOpportunity(Sample const& baseline, Sample const& probe) {
    if (probe.diedBeforeAction)
        return false;
    if (probe.activatedRing && !baseline.activatedRing)
        return true;
    if (probe.surviveBin > baseline.surviveBin)
        return true;
    float const gain = probe.endX - baseline.endX;
    if (!(gain >= EbOpportunityProbe::kEndXGain)) {
        return false;
    }
    return true;
}

bool EbOpportunityProbe::shouldSearchFurther(bool foresightOrbPad, bool orbWindowFound,
                                           bool d0ActivatedRing, Sample const& baseline,
                                           Sample const* probes, int probeCount) {
    if (foresightOrbPad)
        return true;
    if (orbWindowFound)
        return true;
    if (d0ActivatedRing)
        return true;
    if (!probes || probeCount <= 0)
        return false;
    for (int index = 0; index < probeCount; ++index) {
        if (EbOpportunityProbe::sampleShowsOpportunity(baseline, probes[index])) {
            return true;
        }
    }
    return false;
}

bool EbOpportunityProbe::allowPassNoThreat(bool foresightOrbPad, bool orbWindowFound,
                                         bool d0ActivatedRing, Sample const& baseline,
                                         Sample const* probes, int probeCount) {
    return !EbOpportunityProbe::shouldSearchFurther(foresightOrbPad, orbWindowFound, d0ActivatedRing,
                                                  baseline, probes, probeCount);
}

bool EbOpportunityProbe::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += label;
    };

    expect(EbOpportunityProbe::midDelay(16) == 8, "mid-16");
    expect(EbOpportunityProbe::midDelay(1) == 1, "mid-1");
    expect(EbOpportunityProbe::midDelay(0) == 0, "mid-0");
    expect(EbOpportunityProbe::midDelay(5) == 2, "mid-5");

    expect(EbOpportunityProbe::orbScanTicks(16, 2) == 22, "scan-16-2");
    expect(EbOpportunityProbe::orbScanTicks(2, 0) == 6, "scan-2-0");
    expect(EbOpportunityProbe::orbScanTicks(0, 0) == 1, "scan-0");
    expect(EbOpportunityProbe::pressReachScanTicks(64, 8) ==
               EbOpportunityProbe::kMaxPressReachExtraTicks,
           "press-reach-cap");

    Sample base{};
    base.surviveBin = 0;
    base.endX = 100.f;

    Sample betterEnd = base;
    betterEnd.endX = 100.f + EbOpportunityProbe::kEndXGain;
    expect(EbOpportunityProbe::sampleShowsOpportunity(base, betterEnd), "opp-endx");

    Sample tinyEnd = base;
    tinyEnd.endX = 100.f + 3.f;
    expect(!EbOpportunityProbe::sampleShowsOpportunity(base, tinyEnd), "opp-endx-small");

    Sample betterBin = base;
    betterBin.surviveBin = 1;
    expect(EbOpportunityProbe::sampleShowsOpportunity(base, betterBin), "opp-bin");

    Sample act = base;
    act.activatedRing = true;
    expect(EbOpportunityProbe::sampleShowsOpportunity(base, act), "opp-ring");

    Sample died = act;
    died.diedBeforeAction = true;
    expect(!EbOpportunityProbe::sampleShowsOpportunity(base, died), "opp-died-before");

    // G2: foresight hit + d0 alive → must search (never PassNoThreat).
    expect(EbOpportunityProbe::shouldSearchFurther(true, false, false, base, nullptr, 0),
           "g2-foresight-search");
    expect(!EbOpportunityProbe::allowPassNoThreat(true, false, false, base, nullptr, 0),
           "g2-foresight-no-pass");

    expect(EbOpportunityProbe::shouldSearchFurther(false, true, false, base, nullptr, 0),
           "g2-orbwin-search");
    expect(EbOpportunityProbe::shouldSearchFurther(false, false, true, base, nullptr, 0),
           "g2-d0-ring-search");

    Sample probes[2];
    probes[0] = betterEnd;
    expect(EbOpportunityProbe::shouldSearchFurther(false, false, false, base, probes, 1),
           "g2-probe-search");
    expect(!EbOpportunityProbe::allowPassNoThreat(false, false, false, base, probes, 1),
           "g2-probe-no-pass");

    Sample emptyProbe = base;
    probes[0] = emptyProbe;
    expect(!EbOpportunityProbe::shouldSearchFurther(false, false, false, base, probes, 1),
           "g2-empty-no-search");
    expect(EbOpportunityProbe::allowPassNoThreat(false, false, false, base, probes, 1),
           "g2-empty-allow-pass");
    expect(EbOpportunityProbe::allowPassNoThreat(false, false, false, base, nullptr, 0),
           "g2-no-probes-allow-pass");

    if (fails == 0) {
        report = "OpportunityProbe selfCheck OK";
        return true;
    }
    report = std::string("OpportunityProbe selfCheck fails: ") + report;
    return false;
}
