#include "input_reconciler.hpp"
bool EbInputReconciler::request(Source source, int player, bool down, long long timeUs) {
    if (player < 0 || player >= 2)
        return false;
    Owner* selected = nullptr;
    Owner* available = nullptr;
    for (auto& owner : m_owners) {
        if (owner.used && owner.player == player && owner.source.device == source.device &&
            owner.source.id == source.id)
            selected = &owner;
        if (!owner.held && !available)
            available = &owner;
    }
    if (!selected)
        selected = available;
    if (!selected) {
        record(source, player, down, timeUs, Disposition::Overflow);
        return false;
    }
    bool const repeated = selected->used && selected->player == player &&
                          selected->source.device == source.device &&
                          selected->source.id == source.id && selected->held == down;
    selected->source = source;
    selected->player = player;
    selected->used = true;
    selected->held = down;
    bool held = false;
    for (auto const& owner : m_owners)
        held |= owner.used && owner.player == player && owner.held;
    auto& current = m_states[player];
    bool const changed = current.requested != held;
    current.physical = held;
    current.requested = held;
    record(source, player, down, timeUs,
           repeated ? Disposition::Repeat
                    : (changed ? Disposition::Requested : Disposition::AggregatedNoEdge));
    return changed;
}
void EbInputReconciler::acknowledge(int player, bool down, long long timeUs) {
    if (player < 0 || player >= 2)
        return;
    m_states[player].dispatched = down;
    record({}, player, down, timeUs, Disposition::Dispatched);
}
EbInputReconciler::State EbInputReconciler::state(int player) const {
    if (player < 0 || player >= 2)
        return {};
    return m_states[player];
}
void EbInputReconciler::reset() {
    m_owners = {};
    m_states = {};
    m_events = {};
    m_sequence = 0;
}
void EbInputReconciler::record(Source source, int player, bool down, long long timeUs,
                               Disposition disposition) {
    auto& event = m_events[m_sequence % m_events.size()];
    event.sequence = ++m_sequence;
    event.source = source;
    event.player = player;
    event.down = down;
    event.timeUs = timeUs;
    event.disposition = disposition;
}
EbInputReconciler::Event const& EbInputReconciler::latest() const {
    return m_events[m_sequence ? (m_sequence - 1) % m_events.size() : 0];
}
