#ifndef LIVE_RESCUE_SEARCH_HPP
#define LIVE_RESCUE_SEARCH_HPP
#include "live_rescue_policy.hpp"
#include <vector>

class EbLiveRescueSearch {
public:
    explicit EbLiveRescueSearch(int limit, bool preferInterior = false);
    std::vector<int> safeBand() const;
    int nextDelay() const;
    void record(int delay, EbLiveRescuePolicy::Observation observation);
    int selectedDelay() const;
    void reserveFallback();

private:
    bool m_preferInterior;
    int m_limit;
    int m_stride;
    int m_safe = -1;
    bool m_fallback = false;
    std::vector<bool> m_tested;
    std::vector<EbLiveRescuePolicy::Observation> m_observations;
};
#endif
