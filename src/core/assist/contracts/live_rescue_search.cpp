#include "live_rescue_search.hpp"
#include <algorithm>

EbLiveRescueSearch::EbLiveRescueSearch(int limit)
    : m_limit(std::max(0, limit)), m_stride(std::clamp((m_limit + 7) / 8, 1, 4)),
      m_tested(static_cast<std::size_t>(m_limit) + 1, false),
      m_observations(m_tested.size(), EbLiveRescuePolicy::Observation::Incomplete) {}

int EbLiveRescueSearch::nextDelay() const {
    if (!m_tested[0])
        return 0;
    if (m_observations[0] != EbLiveRescuePolicy::Observation::Dead)
        return -1;
    if (m_safe > 0) {
        // Refine only the bracket preceding a proved survivor; never infer a
        // survivor from an interval, and never replace proof with a score.
        for (int delay = std::max(1, m_safe - m_stride + 1); delay < m_safe; ++delay) {
            if (!m_tested[static_cast<std::size_t>(delay)])
                return delay;
        }
        return -1;
    }
    if (!m_fallback) {
        for (int delay = 1; delay <= m_limit; delay += m_stride) {
            if (!m_tested[static_cast<std::size_t>(delay)])
                return delay;
        }
        if (!m_tested[static_cast<std::size_t>(m_limit)])
            return m_limit;
    }
    // Coarse misses do not exclude narrow windows. Exhaust every untested tick
    // once if no coarse survivor was found, subject to the same press budget.
    for (int delay = 1; delay <= m_limit; ++delay) {
        if (!m_tested[static_cast<std::size_t>(delay)])
            return delay;
    }
    return -1;
}

void EbLiveRescueSearch::record(int delay, EbLiveRescuePolicy::Observation observation) {
    if (delay < 0 || delay > m_limit || m_tested[static_cast<std::size_t>(delay)])
        return;
    m_tested[static_cast<std::size_t>(delay)] = true;
    m_observations[static_cast<std::size_t>(delay)] = observation;
    if (delay > 0 && observation == EbLiveRescuePolicy::Observation::Safe &&
        (m_safe < 0 || delay < m_safe))
        m_safe = delay;
}

int EbLiveRescueSearch::selectedDelay() const {
    return EbLiveRescuePolicy::choose(m_observations);
}

void EbLiveRescueSearch::reserveFallback() {
    m_fallback = true;
}
