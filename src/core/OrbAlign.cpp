#include "OrbAlign.hpp"

#include <algorithm>
#include <fmt/format.h>

int OrbAlign::clampFireIntoOrbWindow(
    int fireTick,
    int pressTick,
    int dMin,
    int dMax,
    int limit
) {
    int fire = fireTick;
    if (dMin >= 0) {
        fire = std::max(fire, pressTick + dMin);
    }
    if (dMax >= 0) {
        fire = std::min(fire, pressTick + dMax);
    }
    if (limit >= 0) {
        fire = std::min(fire, pressTick + limit);
    }
    return fire;
}

int OrbAlign::waitUntilInsideRing(int fireTick, int pressTick, int dMin) {
    if (dMin < 0) return fireTick;
    return std::max(fireTick, pressTick + dMin);
}

bool OrbAlign::wrongOrbReject(int actDelay, int dMin, int dMax) {
    if (dMin < 0 && dMax < 0) return false;
    if (dMin >= 0 && actDelay < dMin) return true;
    if (dMax >= 0 && actDelay > dMax) return true;
    return false;
}

bool OrbAlign::robustNeighborsOk(
    bool centerOk,
    bool hasLo,
    bool loOk,
    bool hasHi,
    bool hiOk
) {
    if (!centerOk) return false;
    if (hasLo && !loOk) return false;
    if (hasHi && !hiOk) return false;
    return true;
}

int OrbAlign::acceptRecheckSlide(
    int oldFire,
    int attemptTick,
    int proposedFire,
    int absMax,
    int robustK,
    int deadline,
    bool planOk
) {
    if (!planOk) return -1;
    if (proposedFire <= attemptTick) return -1;

    int const k = std::clamp(robustK, 0, 3);
    int safeMax = absMax;
    if (k > 0) {
        safeMax = absMax - k;
        if (safeMax <= attemptTick) {
            safeMax = absMax;
        }
    }
    proposedFire = std::min(proposedFire, safeMax);
    proposedFire = std::min(proposedFire, deadline);
    if (proposedFire <= attemptTick) return -1;

    // Never earlier than armed fire.
    if (proposedFire < oldFire) return -1;
    // Late slide ≤+1 (orb/toggle and non-orb).
    if (proposedFire > oldFire + kRecheckMaxSlide) {
        proposedFire = oldFire + kRecheckMaxSlide;
    }
    if (proposedFire <= attemptTick) return -1;
    if (proposedFire == oldFire) return -1;
    return proposedFire;
}

bool OrbAlign::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (ok) return;
        ++fails;
        if (!report.empty()) report += "; ";
        report += name;
    };

    expect(kRecheckMaxSlide == 1, "slide-cap-1");

    expect(
        clampFireIntoOrbWindow(10, 0, 4, 8, 16) == 10, "clamp-in"
);
    expect(
        clampFireIntoOrbWindow(2, 0, 4, 8, 16) == 4, "clamp-lo"
);
    expect(
        clampFireIntoOrbWindow(12, 0, 4, 8, 16) == 8, "clamp-hi"
);
    expect(
        waitUntilInsideRing(3, 0, 5) == 5, "wait-ring"
);
    expect(
        waitUntilInsideRing(7, 0, 5) == 7, "already-in"
);

    expect(!wrongOrbReject(5, 4, 8), "ok-in-win");
    expect(wrongOrbReject(2, 4, 8), "early-wrong");
    expect(wrongOrbReject(9, 4, 8), "late-wrong");
    expect(!wrongOrbReject(3, -1, -1), "no-bounds");

    expect(robustNeighborsOk(true, true, true, true, true), "rob-ok");
    expect(!robustNeighborsOk(false, true, true, true, true), "rob-center");
    expect(!robustNeighborsOk(true, true, false, true, true), "rob-lo");
    expect(!robustNeighborsOk(true, false, false, true, false), "rob-hi");
    expect(robustNeighborsOk(true, false, false, false, false), "rob-edge");

    expect(acceptRecheckSlide(10, 8, 12, 30, 0, 30, true) == 11, "slide-plus1");
    expect(acceptRecheckSlide(10, 8, 11, 30, 0, 30, true) == 11, "slide-exact1");
    expect(acceptRecheckSlide(10, 8, 9, 30, 0, 30, true) < 0, "no-earlier");
    expect(acceptRecheckSlide(10, 8, 10, 30, 0, 30, true) < 0, "noop");
    expect(acceptRecheckSlide(10, 8, 12, 30, 0, 30, false) < 0, "bad-plan");

    if (fails == 0) {
        report = "OrbAlign selfCheck OK";
        return true;
    }
    report = fmt::format("OrbAlign selfCheck {} fails: {}", fails, report);
    return false;
}
