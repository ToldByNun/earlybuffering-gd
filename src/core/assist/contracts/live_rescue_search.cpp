#include "live_rescue_search.hpp"
#include <algorithm>

EbLiveRescueSearch::EbLiveRescueSearch(int limit, bool preferInterior)
    : m_preferInterior(preferInterior), m_limit(std::max(0, limit)),
      m_stride(std::clamp((m_limit + 7) / 8, 1, 4)),
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
        // Nova searches a verified safe band rather than committing its first
        // boundary tick. Bound the extra work and keep an existing proof when
        // the shared wall is under pressure. Never infer safety across gaps.
        if (m_preferInterior && !m_fallback) {
            int const end = std::min(m_limit, m_safe + 4);
            for (int delay = m_safe + 1; delay <= end; ++delay) {
                if (!m_tested[static_cast<std::size_t>(delay)])
                    return delay;
            }
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
    int const first = EbLiveRescuePolicy::choose(m_observations);
    if (!m_preferInterior || first <= 0)
        return first;
    auto const band = safeBand();
    if (band.empty())
        return first;
    return band[(band.size() - 1) / 2];
}

std::vector<int> EbLiveRescueSearch::safeBand() const {
    std::vector<int> best;
    std::vector<int> current;
    for (int delay = 1; delay <= m_limit; ++delay) {
        if (m_tested[static_cast<std::size_t>(delay)] &&
            m_observations[static_cast<std::size_t>(delay)] ==
                EbLiveRescuePolicy::Observation::Safe) {
            current.push_back(delay);
            if (current.size() > best.size())
                best = current;
        } else {
            current.clear();
        }
    }
    return best;
}

void EbLiveRescueSearch::reserveFallback() {
    m_fallback = true;
}
