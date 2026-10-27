#include "AssistPerfStats.hpp"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

AssistPerfStats& AssistPerfStats::get() {
    static AssistPerfStats inst;
    return inst;
}

void AssistPerfStats::notePlan(int micros, bool budgetHit) {
    m_us[m_i] = std::max(0, micros);
    m_hit[m_i] = budgetHit;
    m_i = (m_i + 1) % N;
    if (m_count < N) ++m_count;
}

float AssistPerfStats::avgMicros() const {
    if (m_count <= 0) return 0.f;
    long long sum = 0;
    for (int i = 0; i < m_count; ++i) sum += m_us[i];
    return static_cast<float>(sum) / static_cast<float>(m_count);
}

float AssistPerfStats::budgetHitRate() const {
    if (m_count <= 0) return 0.f;
    int hits = 0;
    for (int i = 0; i < m_count; ++i) if (m_hit[i]) ++hits;
    return static_cast<float>(hits) / static_cast<float>(m_count);
}

std::string AssistPerfStats::overlayBit() const {
    if (m_count <= 0) return {};
    return fmt::format("plan {:.0f}us hit{:.0f}%", avgMicros(), budgetHitRate() * 100.f);
}