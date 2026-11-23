#ifndef COMPLETED_TRIGGER_COLLECTION_HPP
#define COMPLETED_TRIGGER_COLLECTION_HPP
#include <cstddef>

class EbCompletedTriggerCollection {
public:
    bool matches(void const* layer, void const* objects, std::size_t count, std::size_t children,
                 std::size_t sections, bool skipDynamic) const;
    void remember(void const* layer, void const* objects, std::size_t count, std::size_t children,
                  std::size_t sections, bool skipDynamic, bool complete);
    void clear();

private:
    void const* m_layer = nullptr;
    void const* m_objects = nullptr;
    std::size_t m_count = 0;
    std::size_t m_children = 0;
    std::size_t m_sections = 0;
    bool m_skipDynamic = false;
    bool m_complete = false;
};
#endif
