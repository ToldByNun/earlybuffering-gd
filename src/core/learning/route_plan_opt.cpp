#include "route_plan_opt.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

EbRoutePlanOpt::WindowBounds EbRoutePlanOpt::computeWindow(int minDelay, int maxDelay,
                                                           int candidateWindow, int limit) {
    WindowBounds w;
    w.minDelay = minDelay;
    w.maxDelay = maxDelay;
    w.candidateWindow = candidateWindow;
    w.limit = limit;
    w.lo = std::max(0, minDelay);
    int const hiCap = std::min(maxDelay, candidateWindow);
    w.hi = std::min(hiCap, std::max(0, limit));
    w.valid = !windowEmpty(w);
    return w;
}

bool EbRoutePlanOpt::windowEmpty(WindowBounds const& w) {
    return w.hi < w.lo;
}

bool EbRoutePlanOpt::windowCacheHit(WindowBounds const& cached, int minDelay, int maxDelay,
                                    int candidateWindow, int limit) {
    return cached.valid && cached.minDelay == minDelay && cached.maxDelay == maxDelay &&
           cached.candidateWindow == candidateWindow && cached.limit == limit;
}

std::vector<int> EbRoutePlanOpt::subsampleDelays(std::vector<int> const& delays, int budget) {
    if (delays.empty() || budget <= 0)
        return {};
    if (static_cast<int>(delays.size()) <= budget)
        return delays;
    std::vector<int> out;
    out.reserve(static_cast<size_t>(budget));
    int const n = static_cast<int>(delays.size());
    // Uniform stride covering [0, n); always include last.
    float const step = static_cast<float>(n - 1) / static_cast<float>(std::max(1, budget - 1));
    int last = -1;
    for (int i = 0; i < budget; ++i) {
        int idx = static_cast<int>(std::lround(static_cast<float>(i) * step));
        if (idx < 0)
            idx = 0;
        if (idx >= n)
            idx = n - 1;
        if (idx == last)
            continue;
        out.push_back(delays[static_cast<size_t>(idx)]);
        last = idx;
    }
    if (!out.empty() && out.back() != delays.back())
        out.push_back(delays.back());
    if (static_cast<int>(out.size()) > budget)
        out.resize(static_cast<size_t>(budget));
    return out;
}

int EbRoutePlanOpt::subsampleStep(int lo, int hi) {
    if (hi < lo)
        return 1;
    int step = (hi - lo) / 60;
    if (step < 1)
        step = 1;
    return step;
}

std::vector<int> EbRoutePlanOpt::linearSubsample(int lo, int hi) {
    std::vector<int> out;
    if (hi < lo)
        return out;
    int const step = subsampleStep(lo, hi);
    for (int d = lo; d <= hi; d += step)
        out.push_back(d);
    if (out.empty())
        out.push_back(lo);
    else if (out.back() != hi)
        out.push_back(hi);
    return out;
}

void EbRoutePlanOpt::reuseIntScratch(std::vector<int>& scratch, size_t reserveHint) {
    reuseScratch(scratch, reserveHint);
}

std::string EbRoutePlanOpt::timingOverlayBit(Timing const& t) {
    if (t.totalUs <= 0 && t.seedUs <= 0 && t.expandUs <= 0 && t.verifyUs <= 0)
        return {};
    return fmt::format("seed={}µs exp={}µs ver={}µs tot={}µs{}", t.seedUs, t.expandUs, t.verifyUs,
                       t.totalUs, t.budgetHit ? " BUD" : "");
}

bool EbRoutePlanOpt::budgetForbidsShadowVerified(bool budgetHit, bool truncated, bool unknown) {
    return budgetHit || truncated || unknown;
}

bool EbRoutePlanOpt::selfCheck(std::string& report) {
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

    auto empty = computeWindow(10, 8, 48, 48);
    expect(windowEmpty(empty), "hi-lt-lo");
    expect(!empty.valid, "empty-invalid");

    auto ok = computeWindow(1, 48, 48, 32);
    expect(ok.valid && ok.lo == 1 && ok.hi == 32, "window-ok");
    expect(windowCacheHit(ok, 1, 48, 48, 32), "cache-hit");
    expect(!windowCacheHit(ok, 1, 48, 48, 16), "cache-miss");

    expect(subsampleStep(0, 60) == 1, "step-60");
    expect(subsampleStep(0, 120) == 2, "step-120");
    expect(linearSubsample(5, 4).empty(), "linear-empty");
    auto lin = linearSubsample(0, 60);
    expect(!lin.empty() && lin.front() == 0 && lin.back() == 60, "linear-ends");

    std::vector<int> dense;
    for (int i = 0; i < 100; ++i)
        dense.push_back(i);
    auto sub = subsampleDelays(dense, 8);
    expect(static_cast<int>(sub.size()) <= 8, "sub-cap");
    expect(!sub.empty() && sub.front() == 0, "sub-front");
    expect(subsampleDelays(dense, 200).size() == dense.size(), "sub-passthrough");
    expect(subsampleDelays({}, 4).empty(), "sub-empty");

    std::vector<int> scratch;
    reuseIntScratch(scratch, 64);
    scratch.push_back(1);
    reuseIntScratch(scratch, 64);
    expect(scratch.empty() && scratch.capacity() >= 64, "scratch-reuse");

    Timing t;
    t.seedUs = 12;
    t.expandUs = 40;
    t.verifyUs = 80;
    t.totalUs = 140;
    auto bit = timingOverlayBit(t);
    expect(bit.find("seed=12µs") != std::string::npos, "overlay-seed");
    expect(bit.find("exp=40µs") != std::string::npos, "overlay-exp");
    expect(bit.find("ver=80µs") != std::string::npos, "overlay-ver");
    t.budgetHit = true;
    expect(timingOverlayBit(t).find("BUD") != std::string::npos, "overlay-bud");

    expect(budgetForbidsShadowVerified(true, false, false), "bud-forbid");
    expect(budgetForbidsShadowVerified(false, true, false), "trunc-forbid");
    expect(budgetForbidsShadowVerified(false, false, true), "unk-forbid");
    expect(!budgetForbidsShadowVerified(false, false, false), "ok-allow");

    if (fails == 0) {
        report = "RoutePlanOpt selfCheck OK";
        return true;
    }
    report = fmt::format("RoutePlanOpt selfCheck {} fails: {}", fails, report);
    return false;
}
