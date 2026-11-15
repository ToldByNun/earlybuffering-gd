#ifndef LIVE_FRAME_BUDGET_SCOPE_HPP
#define LIVE_FRAME_BUDGET_SCOPE_HPP

#include "../../contracts/frame_plan_budget.hpp"
#include <cstdint>

class EbLiveFrameBudgetScope {
public:
    explicit EbLiveFrameBudgetScope(EbFramePlanBudget& budget);
    ~EbLiveFrameBudgetScope();
    EbLiveFrameBudgetScope(EbLiveFrameBudgetScope const&) = delete;
    EbLiveFrameBudgetScope& operator=(EbLiveFrameBudgetScope const&) = delete;

    int remaining(int limitUs) const;
    int spentBeforeUs() const;
    std::uint64_t frame() const;
    static long long nowUs();
    static constexpr int kExitReserveUs = 100;

private:
    EbFramePlanBudget& m_budget;
    std::uint64_t m_frame = 0;
    int m_spentBeforeUs = 0;
};

#endif
