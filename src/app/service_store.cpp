#include "service_store.hpp"

EbServiceStore::EbServiceStore(void (*prepare)(std::size_t)) : m_prepare(prepare) {}

EbServiceStore::~EbServiceStore() {
    m_stopping = true;
    // Dependents were constructed after their dependencies, so destroy them first.
    for (auto entry = m_creationOrder.rbegin(); entry != m_creationOrder.rend(); ++entry) {
        auto& slot = m_slots[*entry];
        auto* instance = slot.instance.exchange(nullptr, std::memory_order_acq_rel);
        slot.destroy(instance);
    }
}
