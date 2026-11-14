#ifndef CAPTURE_INDEX_KEY_HPP
#define CAPTURE_INDEX_KEY_HPP

#include <cstddef>

class EbCaptureIndexKey {
public:
    bool matches(void const* layer, void const* source, std::size_t count) const;
    void bind(void const* layer, void const* source, std::size_t count);
    void reset();

private:
    void const* m_layer = nullptr;
    void const* m_source = nullptr;
    std::size_t m_count = 0;
};

#endif
