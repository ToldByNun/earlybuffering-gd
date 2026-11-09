#ifndef CLASS_MANAGER_HPP
#define CLASS_MANAGER_HPP

#include "service_store.hpp"

class PlayLayer;

class EbClassManager {
public:
    static EbClassManager& get();

    template <class Service> Service& service() {
        return m_services.service<Service>();
    }

    void initialize();
    void refreshSettings();
    void onLevelInit(PlayLayer* layer);
    void resetAttempt(PlayLayer* layer);
    void beforeLevelReset(PlayLayer* layer);
    void onCheckpointLoad(PlayLayer* layer);
    void onLevelComplete(PlayLayer* layer);
    void onLevelQuit(PlayLayer* layer);

private:
    EbClassManager();
    EbClassManager(EbClassManager const&) = delete;
    EbClassManager& operator=(EbClassManager const&) = delete;

    static void prepareService(std::size_t serviceIndex);
    static void runStartupChecks();
    void syncDiagnostics();

    EbServiceStore m_services;
    PlayLayer* m_level = nullptr;
    PlayLayer* m_closedLevel = nullptr;
    bool m_initialized = false;
    bool m_quitting = false;
};

#endif
