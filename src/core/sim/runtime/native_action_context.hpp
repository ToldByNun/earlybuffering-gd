#ifndef NATIVE_ACTION_CONTEXT_HPP
#define NATIVE_ACTION_CONTEXT_HPP

#include <Geode/Geode.hpp>
#include <atomic>

class EbNativeActionContext {
public:
    explicit EbNativeActionContext(cocos2d::CCActionManager* manager);
    ~EbNativeActionContext();
    EbNativeActionContext(EbNativeActionContext const&) = delete;
    EbNativeActionContext& operator=(EbNativeActionContext const&) = delete;

    static cocos2d::CCActionManager* redirect(cocos2d::CCActionManager* requested);
    static unsigned long long redirectedCalls();

private:
    cocos2d::CCActionManager* m_previous = nullptr;
    static thread_local cocos2d::CCActionManager* s_current;
    static std::atomic<unsigned long long> s_redirected;
};

#endif
