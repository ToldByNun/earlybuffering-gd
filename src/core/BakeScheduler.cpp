#include "BakeScheduler.hpp"
#include "../debug/AssistLog.hpp"
#include <Geode/Geode.hpp>
using namespace geode::prelude;

BakeScheduler& BakeScheduler::get() {
    static BakeScheduler inst;
    return inst;
}

bool BakeScheduler::running() const { return m_running || BakeMap::get().isBaking(); }

void BakeScheduler::cancel() {
    BakeMap::get().progress().cancel = true;
}

bool BakeScheduler::start(PlayLayer* layer, int startSlot, int radius, BakeMethod method) {
    if (!layer || running()) return false;
    if (startSlot < 0) {
        AssistLog::info("BAKE failed: no start snapshot slot");
        FLAlertLayer::create(
            "EarlyBuffer",
            "No start snapshot — reset the level once, then bake.",
            "OK"
        )->show();
        return false;
    }
    m_running = true;
    // Main-thread only: queue work via queueInMainThread chain (cooperative).
    auto* pl = layer;
    Loader::get()->queueInMainThread([pl, startSlot, radius, method]() {
        auto res = BakeMap::get().bake(pl, startSlot, radius, method);
        BakeScheduler::get().markFinished();
        if (res.isErr()) {
            AssistLog::info(fmt::format("BAKE failed: {}", res.unwrapErr()));
            FLAlertLayer::create("EarlyBuffer", res.unwrapErr().c_str(), "OK")->show();
            return;
        }
        int const n = res.unwrap();
        if (n <= 0) {
            // Belt: bake*() should Err on empty; never celebrate 0 windows.
            char const* msg =
                "Bake produced 0 windows — reset the level, then try again.";
            AssistLog::info(fmt::format("BAKE done clicks=0 (treated as fail)"));
            FLAlertLayer::create("EarlyBuffer", msg, "OK")->show();
            return;
        }
        AssistLog::info(fmt::format("BAKE done clicks={} (scheduler ok)", n));
        FLAlertLayer::create(
            "EarlyBuffer",
            fmt::format("Bake done — {} windows.", n).c_str(),
            "OK"
        )->show();
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
