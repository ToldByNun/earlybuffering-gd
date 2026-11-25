#include "completed_trigger_collection.hpp"

bool EbCompletedTriggerCollection::isComplete(std::size_t expectedCount, std::size_t collectedCount,
                                              std::size_t currentCount, bool incomplete) {
    return !incomplete && expectedCount > 0 && collectedCount == expectedCount &&
           currentCount == expectedCount;
}

bool EbCompletedTriggerCollection::matches(void const* layer, void const* objects,
                                           std::size_t count, std::size_t children,
                                           std::size_t sections, bool skipDynamic) const {
    (void)children;
    (void)sections;
    // Render batches and collision sections change during play. Only the owning
    // source defines whether the collected level object set has changed.
    return m_complete && layer && objects && layer == m_layer && objects == m_objects &&
           count == m_count && skipDynamic == m_skipDynamic;
}

void EbCompletedTriggerCollection::remember(void const* layer, void const* objects,
                                            std::size_t count, std::size_t children,
                                            std::size_t sections, bool skipDynamic, bool complete) {
    clear();
    if (!complete || !layer || !objects || count == 0)
        return;
    m_layer = layer;
    m_objects = objects;
    m_count = count;
    m_children = children;
    m_sections = sections;
    m_skipDynamic = skipDynamic;
    m_complete = true;
}

void EbCompletedTriggerCollection::clear() {
    m_complete = false;
    m_layer = nullptr;
    m_objects = nullptr;
}
