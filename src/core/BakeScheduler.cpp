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
    m_running = true;
    // Main-thread only: queue work via queueInMainThread chain (cooperative).
    auto* pl = layer;
    Loader::get()->queueInMainThread([pl, startSlot, radius, method]() {
        auto res = BakeMap::get().bake(pl, startSlot, radius, method);
        BakeScheduler::get().markFinished();
        if (res.isErr()) {
            AssistLog::info(fmt::format("BAKE failed: {}", res.unwrapErr()));
            FLAlertLayer::create("EarlyBuffer", res.unwrapErr().c_str(), "OK")->show();
        } else {
            AssistLog::info(fmt::format("BAKE done: {} windows", res.unwrap()));
            FLAlertLayer::create(
                "EarlyBuffer",
                fmt::format("Bake done — {} windows (main-thread).", res.unwrap()).c_str(),
                "OK"
            )->show();
        }
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