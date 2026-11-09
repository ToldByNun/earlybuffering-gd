#ifndef SERVICE_STORE_HPP
#define SERVICE_STORE_HPP

#include "eb_class_catalog.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <vector>

// One owner for service instances; successful accesses avoid the construction lock.
class EbServiceStore {
public:
    explicit EbServiceStore(void (*prepare)(std::size_t) = nullptr);
    ~EbServiceStore();
    EbServiceStore(EbServiceStore const&) = delete;
    EbServiceStore& operator=(EbServiceStore const&) = delete;

    template <class Service> Service& service() {
        constexpr auto index = EbServiceTraits<Service>::index;
        static_assert(index < EB_MANAGED_SERVICE_COUNT);
        auto& slot = m_slots[index];
        if (auto* instance = slot.instance.load(std::memory_order_acquire))
            return *static_cast<Service*>(instance);

        std::lock_guard<std::recursive_mutex> lock(m_constructionMutex);
        if (auto* instance = slot.instance.load(std::memory_order_acquire))
            return *static_cast<Service*>(instance);
        if (m_stopping)
            throw std::logic_error("Service construction during shutdown");
        if (slot.constructing)
            throw std::logic_error("Service dependency cycle during construction");

        slot.constructing = true;
        try {
            if (m_prepare)
                m_prepare(index);
            // Reserve teardown bookkeeping before construction can acquire resources.
            m_creationOrder.reserve(EB_MANAGED_SERVICE_COUNT);
            auto* instance = new Service;
            slot.destroy = [](void* value) { delete static_cast<Service*>(value); };
            m_creationOrder.push_back(index);
            slot.constructing = false;
            slot.instance.store(instance, std::memory_order_release);
            return *instance;
        } catch (...) {
            slot.constructing = false;
            throw;
        }
    }

private:
    class Slot {
    public:
        Slot() = default;

    private:
        friend class EbServiceStore;
        std::atomic<void*> instance = nullptr;
        void (*destroy)(void*) = nullptr;
        bool constructing = false;
    };

    std::array<Slot, EB_MANAGED_SERVICE_COUNT> m_slots{};
    std::vector<std::size_t> m_creationOrder;
    std::recursive_mutex m_constructionMutex;
    void (*m_prepare)(std::size_t);
    bool m_stopping = false;
};

#endif
