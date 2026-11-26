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
    // A fixed 4ms floor consumed essentially a whole 240Hz frame before drawing.
    return EbFramePlanBudget::limitForInterval(
        configuredUs, cocos2d::CCDirector::sharedDirector()->getAnimationInterval(),
        cocos2d::CCDirector::sharedDirector()->getActualDeltaTime());
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
