#include "assist_log.hpp"
#include "../config/stage_config.hpp"
#include <Geode/Geode.hpp>
#include <fmt/format.h>

char const* EbAssistLog::modTag() {
    return "EarlyBuffer";
}

double EbAssistLog::ticksToFrames60(int ticks, double physicsTps) {
    if (physicsTps <= 0.0)
        physicsTps = 240.0;
    return static_cast<double>(ticks) * (60.0 / physicsTps);
}

void EbAssistLog::info(std::string const& message) {
    geode::log::info("[{}] {}", modTag(), message);
}

void EbAssistLog::debug(std::string const& message) {
    if (!EbStageConfig::debugLogging())
        return;
    geode::log::info("[{}] {}", modTag(), message);
}

void EbAssistLog::printBanner() {
    auto const stage = EbStageConfig::fromSettings();
    auto const version = geode::prelude::Mod::get()->getVersion().toVString();
    geode::log::info("======================");
    geode::log::info("[{}] {} LOADED", modTag(), version);
    if (!stage.enabled) {
        geode::log::info("[{}] Starting Mode: OFF", modTag());
    } else {
        geode::log::info("[{}] Starting windows: {} ALL / {} Wave (slack {})", modTag(),
                         stage.ticksAll, stage.ticksWave, EbStageConfig::deathSlack());
    }
    geode::log::info(
        "[{}] Ctrl+Shift+M: modes  |  Ctrl+Shift+T: trajectory  |  Ctrl+Shift+L: learning",
        modTag());
    geode::log::info("[{}] Assist mode: {} ({})", modTag(),
                     EbStageConfig::assistModeName(EbStageConfig::assistMode()),
                     EbStageConfig::assistModeHint(EbStageConfig::assistMode()));
    if (EbStageConfig::debugLogging()) {
        geode::log::info("[{}] Trigger-world manual edge matrix:", modTag());
        geode::log::info("[{}]   1) static move corridor — bake ready → Include, pose follows keys",
                         modTag());
        geode::log::info(
            "[{}]   2) follow-player spike — DynamicFollow → Unknown (skip-dynamic on)", modTag());
        geode::log::info("[{}]   3) cache hit 2nd enter — loadOrBuild, no rebuild / no hitch",
                         modTag());
        geode::log::info("[{}]   4) skip-dynamic on — no live probe; targets stay Unknown",
                         modTag());
        geode::log::info("[{}]   5) quit mid-build — cancel, save dirty partial, clear", modTag());
        geode::log::info(
            "[{}]   6) death/reset LagProbe — no eb.triggerBake.build inside death episode",
            modTag());
        geode::log::info(
            "[{}]      (build only via tickSlice / loadOrBuild; death cancels, reset may resume)",
            modTag());
    }
    geode::log::info("======================");
}

void EbAssistLog::printSavedPress(int delayedTicks, int frame0, int delayedFrame, int limit,
                                double physicsTps) {
    if (!EbStageConfig::debugLogging())
        return;
    auto const frames60 = ticksToFrames60(delayedTicks, physicsTps);
    geode::log::info("[{}] *** SAVED +{} TICKS ({:.2f} frames @ 60fps) PRESS! (Frame0: {} -> "
                     "Delayed: {}) [Limit: {}] ****",
                     modTag(), delayedTicks, frames60, frame0, delayedFrame, limit);
}
