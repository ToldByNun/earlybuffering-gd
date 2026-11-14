#ifndef NATIVE_MEMORY_GUARD_HPP
#define NATIVE_MEMORY_GUARD_HPP

#include <atomic>
#include <mutex>

class EbNativeMemoryGuard {
public:
    class Lock {
    public:
        Lock();

    private:
        std::unique_lock<std::recursive_mutex> m_lock;
    };

    EbNativeMemoryGuard();
    ~EbNativeMemoryGuard();
    EbNativeMemoryGuard(EbNativeMemoryGuard const&) = delete;
    EbNativeMemoryGuard& operator=(EbNativeMemoryGuard const&) = delete;

    void drain();
    static void noteAutorelease();
    static unsigned long long autoreleaseCount();

private:
    static std::atomic<int> s_users;
    static std::atomic<unsigned long long> s_autoreleases;
    static std::recursive_mutex s_mutex;
    bool m_poolOpen = false;
};

#endif
