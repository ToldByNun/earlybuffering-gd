#include "bake_scheduler.hpp"
#include "../debug/assist_log.hpp"
#include <Geode/Geode.hpp>

BakeScheduler& BakeScheduler::get() {
    static BakeScheduler inst;
    return inst;
}

bool BakeScheduler::running() const {
    return m_running || BakeMap::get().isBaking();
}

void BakeScheduler::cancel() {
    BakeMap::get().progress().cancel = true;
}

bool BakeScheduler::start(PlayLayer* layer, int startSlot, int radius, BakeMethod method) {
    if (!layer || running())
        return false;
    if (startSlot < 0) {
        AssistLog::info("BAKE failed: no start snapshot slot");
        FLAlertLayer::create("EarlyBuffer", "No start snapshot — reset the level once, then bake.",
                             "OK")
            ->show();
        return false;
    }
    m_running = true;
    // Main-thread only: queue work via queueInMainThread chain (cooperative).
    auto* playLayer = layer;
    geode::prelude::Loader::get()->queueInMainThread([playLayer, startSlot, radius, method]() {
        auto result = BakeMap::get().bake(playLayer, startSlot, radius, method);
        BakeScheduler::get().markFinished();
        if (result.isErr()) {
            AssistLog::info(fmt::format("BAKE failed: {}", result.unwrapErr()));
            FLAlertLayer::create("EarlyBuffer", result.unwrapErr().c_str(), "OK")->show();
            return;
        }
        int const count = result.unwrap();
        if (count <= 0) {
            // Belt: bake*() should Err on empty; never celebrate 0 windows.
            char const* message = "Bake produced 0 windows — reset the level, then try again.";
            AssistLog::info(fmt::format("BAKE done clicks=0 (treated as fail)"));
            FLAlertLayer::create("EarlyBuffer", message, "OK")->show();
            return;
        }
        auto const status = BakeMap::get().progress().getStatus();
        AssistLog::info(fmt::format("BAKE done clicks={} status={} (scheduler ok)", count, status));
        std::string message = fmt::format("Bake done — {} windows.", count);
        if (status.find("partial") != std::string::npos) {
            message += "\n(";
            message += status;
            message += " — Hybrid part OK)";
        }
        FLAlertLayer::create("EarlyBuffer", message.c_str(), "OK")->show();
    });
    return true;
}

void BakeScheduler::tickSlice() {
    // bake() currently runs to completion on the queued main-thread job;
    // cancel is cooperative via BakeMap::progress().cancel.
}

void BakeScheduler::markFinished() {
    m_running = false;
}
