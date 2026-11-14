#include "native_memory_guard.hpp"
#include <Geode/Geode.hpp>

std::atomic<int> EbNativeMemoryGuard::s_users{0};
std::atomic<unsigned long long> EbNativeMemoryGuard::s_autoreleases{0};
std::recursive_mutex EbNativeMemoryGuard::s_mutex;

EbNativeMemoryGuard::Lock::Lock() : m_lock(s_mutex, std::defer_lock) {
    if (s_users.load(std::memory_order_acquire) > 0)
        m_lock.lock();
}

EbNativeMemoryGuard::EbNativeMemoryGuard() {
    s_users.fetch_add(1, std::memory_order_acq_rel);
    cocos2d::CCPoolManager::sharedPoolManager()->push();
    m_poolOpen = true;
}

EbNativeMemoryGuard::~EbNativeMemoryGuard() {
    drain();
    s_users.fetch_sub(1, std::memory_order_acq_rel);
}

void EbNativeMemoryGuard::drain() {
    if (!m_poolOpen)
        return;
    m_poolOpen = false;
    cocos2d::CCPoolManager::sharedPoolManager()->pop();
}

void EbNativeMemoryGuard::noteAutorelease() {
    if (s_users.load(std::memory_order_acquire) > 0)
        s_autoreleases.fetch_add(1, std::memory_order_relaxed);
}

unsigned long long EbNativeMemoryGuard::autoreleaseCount() {
    return s_autoreleases.load(std::memory_order_relaxed);
}
