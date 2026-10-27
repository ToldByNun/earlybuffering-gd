#ifndef FRIRECRACKER_CORE_ASSISTPERFSTATS_HPP
#define FRIRECRACKER_CORE_ASSISTPERFSTATS_HPP

#include <string>

class AssistPerfStats {
public:
    static AssistPerfStats& get();
    void notePlan(int micros, bool budgetHit);
    float avgMicros() const;
    float budgetHitRate() const;
    std::string overlayBit() const;
private:
    static constexpr int N = 32;
    int m_us[N]{};
    bool m_hit[N]{};
    int m_i = 0;
    int m_count = 0;
};

#endif