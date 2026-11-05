#include "BakeScheduler.hpp"
#include "../config/StageConfig.hpp"
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
        auto const status = BakeMap::get().progress().getStatus();
        AssistLog::info(fmt::format(
            "BAKE done clicks={} status={} (scheduler ok)", n, status
        ));
        std::string msg = fmt::format("Bake done — {} windows.", n);
        if (status.find("partial") != std::string::npos) {
            if (StageConfig::assistMode() == AssistMode::Route) {
                // A partial replay still has useful click windows, but must
                // never be presented as a usable Route: it has no complete
                // validated pose trace and therefore no route.json.
                msg = "Replay validation was partial — Route was not created.\n\n"
                    "The click windows were saved for Replay/Hybrid, but Route needs "
                    "a replay that survives validation from this exact StartPos.\n\n(";
                msg += status;
                msg += ")";
            }
            else {
                msg += "\n(";
                msg += status;
                msg += " — Hybrid part OK)";
            }
        }
        FLAlertLayer::create("EarlyBuffer", msg.c_str(), "OK")->show();
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
