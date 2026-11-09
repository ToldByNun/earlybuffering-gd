#include "decision_log.hpp"

void EbDecisionLog::clear() {
    m_entries.clear();
}

EbDecisionEntry& EbDecisionLog::push(EbDecisionEntry entry) {
    if (static_cast<int>(m_entries.size()) >= CAPACITY) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back(std::move(entry));
    return m_entries.back();
}

EbDecisionEntry* EbDecisionLog::latest() {
    return m_entries.empty() ? nullptr : &m_entries.back();
}

EbDecisionEntry const* EbDecisionLog::latest() const {
    return m_entries.empty() ? nullptr : &m_entries.back();
}

void EbDecisionLog::noteInput(int tick, bool down) {
    for (auto& e : m_entries) {
        if (e.resolved)
            continue;
        e.timeline.push_back(EbInputEvent{tick, down});
    }
}

EbDecisionEntry* EbDecisionLog::findForDeath(int tick) {
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (it->resolved)
            continue;
        if (tick < it->tick)
            continue;
        if (tick - it->tick > it->horizon + it->limit + 8)
            continue;
        return &(*it);
    }
    return nullptr;
}
