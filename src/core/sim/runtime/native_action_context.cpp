#include "native_action_context.hpp"

thread_local cocos2d::CCActionManager* EbNativeActionContext::s_current = nullptr;
std::atomic<unsigned long long> EbNativeActionContext::s_redirected{0};

EbNativeActionContext::EbNativeActionContext(cocos2d::CCActionManager* manager)
    : m_previous(s_current) {
    s_current = manager;
}

EbNativeActionContext::~EbNativeActionContext() {
    s_current = m_previous;
}

cocos2d::CCActionManager* EbNativeActionContext::redirect(cocos2d::CCActionManager* requested) {
    if (!s_current || requested == s_current)
        return nullptr;
    s_redirected.fetch_add(1, std::memory_order_relaxed);
    return s_current;
}

unsigned long long EbNativeActionContext::redirectedCalls() {
    return s_redirected.load(std::memory_order_relaxed);
}
