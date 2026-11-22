#include "live_frame_budget_scope.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

long long EbLiveFrameBudgetScope::nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

EbLiveFrameBudgetScope::EbLiveFrameBudgetScope(EbFramePlanBudget& budget) : m_budget(budget) {
    m_frame = cocos2d::CCDirector::sharedDirector()->getTotalFrames();
    auto const now = nowUs();
    m_budget.begin(m_frame, now);
    m_spentBeforeUs = static_cast<int>(
        std::min(m_budget.spent(now), static_cast<long long>(std::numeric_limits<int>::max())));
}

EbLiveFrameBudgetScope::~EbLiveFrameBudgetScope() {
    m_budget.end(nowUs());
}

int EbLiveFrameBudgetScope::frameLimit(int configuredUs) {
    if (configuredUs == 0)
        return 0;
    double interval = cocos2d::CCDirector::sharedDirector()->getAnimationInterval();
    if (!std::isfinite(interval) || interval <= 0.)
        interval = 1. / 240.;
    // ~40% of the display frame, capped at 4ms. Floor 3ms so 240Hz displays do not
    // starve Stage-12 windows (Nova slow-click bar ~4ms; pack 191833 had only 1666µs).
    int const fromInterval = static_cast<int>(std::clamp(interval * 400000., 1., 4000.));
    int const floorUs = std::min(configuredUs, 3000);
    return std::min(configuredUs, std::max(fromInterval, floorUs));
}

int EbLiveFrameBudgetScope::remaining(int limitUs) const {
    if (limitUs == 0)
        return std::numeric_limits<int>::max();
    return m_budget.remaining(limitUs, nowUs(), kExitReserveUs);
}

int EbLiveFrameBudgetScope::spentBeforeUs() const {
    return m_spentBeforeUs;
}
std::uint64_t EbLiveFrameBudgetScope::frame() const {
    return m_frame;
}
