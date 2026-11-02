#include "TrajDrawHorizon.hpp"
#include "LongForesight.hpp"

#include <algorithm>
#include <fmt/format.h>

int TrajDrawHorizon::holdCoastDrawMax() {
    return kDrawUntilDeathMax;
}

int TrajDrawHorizon::predHorizon(int scoreHorizon) {
    return std::max(LongForesight::kMinHorizon, std::max(0, scoreHorizon));
}

int TrajDrawHorizon::rebuildPeriod(int drawMax) {
    if (drawMax > kLongPeriodThreshold) return kRebuildPeriodLong;
    return kRebuildPeriodShort;
}

bool TrajDrawHorizon::hitDrawCap(bool died, int pointsOrFrames, int drawMax) {
    if (died || drawMax <= 0) return false;
    return pointsOrFrames >= drawMax;
}

bool TrajDrawHorizon::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(kDrawUntilDeathMax == 960, "max-960");
    expect(kDrawUntilDeathMax > 320, "max-gt-lf320");
    expect(holdCoastDrawMax() == kDrawUntilDeathMax, "hold-coast-max");
    expect(predHorizon(40) == LongForesight::kMinHorizon, "pred-floor-48");
    expect(predHorizon(160) == 160, "pred-score");
    expect(predHorizon(960) == 960, "pred-can-be-long-if-score");
    expect(rebuildPeriod(56) == kRebuildPeriodShort, "period-short");
    expect(rebuildPeriod(97) == kRebuildPeriodLong, "period-long");
    expect(rebuildPeriod(960) == kRebuildPeriodLong, "period-max");
    expect(!hitDrawCap(true, 960, 960), "cap-not-on-death");
    expect(hitDrawCap(false, 960, 960), "cap-alive-at-max");
    expect(!hitDrawCap(false, 100, 960), "cap-not-early");

    if (fails == 0) {
        report = "TrajDrawHorizon selfCheck OK";
        return true;
    }
    report = fmt::format("TrajDrawHorizon selfCheck {} fails: {}", fails, report);
    return false;
}
