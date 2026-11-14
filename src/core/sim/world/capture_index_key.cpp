#include "capture_index_key.hpp"

bool EbCaptureIndexKey::matches(void const* layer, void const* source, std::size_t count) const {
    return layer && source && m_layer == layer && m_source == source && m_count == count;
}

void EbCaptureIndexKey::bind(void const* layer, void const* source, std::size_t count) {
    m_layer = layer;
    m_source = source;
    m_count = count;
}

void EbCaptureIndexKey::reset() {
    m_layer = nullptr;
    m_source = nullptr;
    m_count = 0;
}
