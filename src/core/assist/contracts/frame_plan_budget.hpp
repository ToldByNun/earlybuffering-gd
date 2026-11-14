#ifndef FRAME_PLAN_BUDGET_HPP
#define FRAME_PLAN_BUDGET_HPP

#include <cstdint>

class EbFramePlanBudget {
public:
    void begin(std::uint64_t frame, long long nowUs);
    void end(long long nowUs);
    int remaining(int limitUs, long long nowUs, int reserveUs = 0) const;
    long long spent(long long nowUs) const;

private:
    std::uint64_t m_frame = 0;
    bool m_hasFrame = false;
    int m_depth = 0;
    long long m_startedUs = 0;
    long long m_spentUs = 0;
};

#endif
