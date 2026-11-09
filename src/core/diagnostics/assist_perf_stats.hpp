#ifndef ASSIST_PERF_STATS_HPP
#define ASSIST_PERF_STATS_HPP

#include <string>

class EbAssistPerfStats {
public:
    static EbAssistPerfStats& get();
    void notePlan(int micros, bool budgetHit);
    float avgMicros() const;
    float budgetHitRate() const;
    std::string overlayBit() const;

private:
    friend class EbServiceStore;

    static constexpr int N = 32;
    int m_us[N]{};
    bool m_hit[N]{};
    int m_i = 0;
    int m_count = 0;
};

#endif
