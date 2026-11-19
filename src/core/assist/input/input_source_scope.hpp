#ifndef INPUT_SOURCE_SCOPE_HPP
#define INPUT_SOURCE_SCOPE_HPP
#include "input_reconciler.hpp"
class EbInputSourceScope {
public:
    explicit EbInputSourceScope(EbInputReconciler::Source source);
    ~EbInputSourceScope();
    EbInputSourceScope(EbInputSourceScope const&) = delete;
    EbInputSourceScope& operator=(EbInputSourceScope const&) = delete;
    static EbInputReconciler::Source current();

private:
    EbInputReconciler::Source m_previous;
    static thread_local EbInputReconciler::Source s_current;
};
#endif
