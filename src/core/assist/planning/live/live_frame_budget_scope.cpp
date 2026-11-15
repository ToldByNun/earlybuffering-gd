#include "live_frame_budget_scope.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
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

int EbLiveFrameBudgetScope::remaining(int limitUs) const {
    return m_budget.remaining(limitUs, nowUs(), kExitReserveUs);
}

int EbLiveFrameBudgetScope::spentBeforeUs() const {
    return m_spentBeforeUs;
}
std::uint64_t EbLiveFrameBudgetScope::frame() const {
    return m_frame;
}
