#include "frame_plan_budget.hpp"
#include <algorithm>
#include <cmath>

int EbFramePlanBudget::limitForInterval(int configuredUs, double intervalSeconds,
                                        double actualIntervalSeconds) {
    if (configuredUs <= 0)
        return 0;
    if (std::isfinite(actualIntervalSeconds) && actualIntervalSeconds > 0. &&
        (!std::isfinite(intervalSeconds) || intervalSeconds <= 0. ||
         actualIntervalSeconds < intervalSeconds))
        intervalSeconds = actualIntervalSeconds;
    if (!std::isfinite(intervalSeconds) || intervalSeconds <= 0.)
        intervalSeconds = 1. / 240.;
    int const frameShareUs = static_cast<int>(std::clamp(intervalSeconds * 400000., 250., 4000.));
    return std::min(configuredUs, frameShareUs);
}

void EbFramePlanBudget::begin(std::uint64_t frame, long long nowUs) {
    if (m_depth > 0) {
        ++m_depth;
        return;
    }
    if (!m_hasFrame || m_frame != frame) {
        m_frame = frame;
        m_hasFrame = true;
        m_spentUs = 0;
    }
    m_startedUs = nowUs;
    m_depth = 1;
}

void EbFramePlanBudget::end(long long nowUs) {
    if (m_depth <= 0)
        return;
    --m_depth;
    if (m_depth == 0)
        m_spentUs += std::max(0LL, nowUs - m_startedUs);
}

long long EbFramePlanBudget::spent(long long nowUs) const {
    return m_spentUs + (m_depth > 0 ? std::max(0LL, nowUs - m_startedUs) : 0LL);
}

int EbFramePlanBudget::remaining(int limitUs, long long nowUs, int reserveUs) const {
    if (limitUs <= 0)
        return 0;
    long long const available =
        static_cast<long long>(limitUs) - spent(nowUs) - std::max(0, reserveUs);
    return static_cast<int>(std::max(0LL, available));
}
