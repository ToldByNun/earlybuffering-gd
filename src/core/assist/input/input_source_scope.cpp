#include "input_source_scope.hpp"
thread_local EbInputReconciler::Source EbInputSourceScope::s_current{};
EbInputSourceScope::EbInputSourceScope(EbInputReconciler::Source source) : m_previous(s_current) {
    s_current = source;
}
EbInputSourceScope::~EbInputSourceScope() {
    s_current = m_previous;
}
EbInputReconciler::Source EbInputSourceScope::current() {
    return s_current;
}
