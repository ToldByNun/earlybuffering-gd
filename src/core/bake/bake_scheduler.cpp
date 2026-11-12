#include "../../app/class_manager.hpp"
#include "bake_scheduler.hpp"
#include "../../debug/assist_log.hpp"
#include <Geode/Geode.hpp>
#include "../../ui/desktop_menu/desktop_menu_alert.hpp"

EbBakeScheduler& EbBakeScheduler::get() {
    return EbClassManager::get().service<EbBakeScheduler>();
}

bool EbBakeScheduler::running() const {
    return m_running || EbBakeMap::get().isBaking();
}

void EbBakeScheduler::cancel() {
    EbBakeMap::get().progress().cancel = true;
}

bool EbBakeScheduler::start(PlayLayer* layer, int startSlot, int radius, BakeMethod method) {
    if (!layer || running())
        return false;
    if (startSlot < 0) {
        EbAssistLog::info("BAKE failed: no start snapshot slot");
        EbDesktopAlert::info("EarlyBuffer", "No start snapshot — reset the level once, then bake.");
        return false;
    }
    m_running = true;
    // Main-thread only: queue work via queueInMainThread chain (cooperative).
    auto* playLayer = layer;
    geode::prelude::Loader::get()->queueInMainThread([playLayer, startSlot, radius, method]() {
        auto result = EbBakeMap::get().bake(playLayer, startSlot, radius, method);
        EbBakeScheduler::get().markFinished();
        if (result.isErr()) {
            EbAssistLog::info(fmt::format("BAKE failed: {}", result.unwrapErr()));
            EbDesktopAlert::info("EarlyBuffer", result.unwrapErr().c_str());
            return;
        }
        int const count = result.unwrap();
        if (count <= 0) {
            // Belt: bake*() should Err on empty; never celebrate 0 windows.
            char const* message = "Bake produced 0 windows — reset the level, then try again.";
            EbAssistLog::info(fmt::format("BAKE done clicks=0 (treated as fail)"));
            EbDesktopAlert::info("EarlyBuffer", message);
            return;
        }
        auto const status = EbBakeMap::get().progress().getStatus();
        EbAssistLog::info(fmt::format("BAKE done clicks={} status={} (scheduler ok)", count, status));
        std::string message = fmt::format("Bake done — {} windows.", count);
        if (status.find("partial") != std::string::npos) {
            message += "\n(";
            message += status;
            message += " — Hybrid part OK)";
        }
        EbDesktopAlert::info("EarlyBuffer", message.c_str());
    });
    return true;
}

void EbBakeScheduler::tickSlice() {
    // bake() currently runs to completion on the queued main-thread job;
    // cancel is cooperative via BakeMap::progress().cancel.
}

void EbBakeScheduler::markFinished() {
    m_running = false;
}
