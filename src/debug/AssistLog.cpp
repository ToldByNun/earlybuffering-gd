#include "AssistLog.hpp"
#include "../config/StageConfig.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>

using namespace geode::prelude;

char const* AssistLog::modTag() {
    return "EarlyBuffer";
}

double AssistLog::ticksToFrames60(int ticks, double physicsTps) {
    if (physicsTps <= 0.0) physicsTps = 240.0;
    return static_cast<double>(ticks) * (60.0 / physicsTps);
}

void AssistLog::info(std::string const& message) {
    log::info("[{}] {}", modTag(), message);
}

void AssistLog::debug(std::string const& message) {
    if (!StageConfig::debugLogging()) return;
    log::info("[{}] {}", modTag(), message);
}

void AssistLog::printBanner() {
    auto const stage = StageConfig::fromSettings();
    auto const version = Mod::get()->getVersion().toVString();
    log::info("======================");
    log::info("[{}] {} LOADED", modTag(), version);
    if (!stage.enabled || stage.index >= StageConfig::STAGE_COUNT - 1) {
        log::info("[{}] Starting Mode: OFF", modTag());
    }
    else {
        log::info(
            "[{}] Starting Preset: {} ({} ALL / {} Wave, slack {})",
            modTag(),
            StageConfig::presetName(stage.index),
            stage.ticksAll,
            stage.ticksWave,
            StageConfig::deathSlack()
        );
    }
    log::info(
        "[{}] F6: presets  |  Ctrl+Shift+M: modes  |  Ctrl+Shift+T: trajectory  |  Ctrl+Shift+L: learning",
        modTag()
    );
    log::info(
        "[{}] Assist mode: {} ({})",
        modTag(),
        StageConfig::assistModeName(StageConfig::assistMode()),
        StageConfig::assistModeHint(StageConfig::assistMode())
    );
    if (StageConfig::debugLogging()) {
        log::info("[{}] Trigger-world manual edge matrix:", modTag());
        log::info("[{}]   1) static move corridor — bake ready → Include, pose follows keys", modTag());
        log::info("[{}]   2) follow-player spike — DynamicFollow → Unknown (skip-dynamic on)", modTag());
        log::info("[{}]   3) cache hit 2nd enter — loadOrBuild, no rebuild / no hitch", modTag());
        log::info("[{}]   4) skip-dynamic on — no live probe; targets stay Unknown", modTag());
        log::info("[{}]   5) quit mid-build — cancel, save dirty partial, clear", modTag());
        log::info("[{}]   6) death/reset LagProbe — no eb.triggerBake.build inside death episode", modTag());
        log::info("[{}]      (build only via tickSlice / loadOrBuild; death cancels, reset may resume)", modTag());
    }
    log::info("======================");
}

void AssistLog::printCycle(StageInfo const& stage, double physicsTps) {
    if (!stage.enabled || stage.index >= StageConfig::STAGE_COUNT - 1) {
        log::info("[{}] F6 Cycle -> OFF", modTag());
        return;
    }

    auto const frames60 = ticksToFrames60(stage.ticksAll, physicsTps);
    if (stage.ticksWave != stage.ticksAll) {
        auto const waveFrames = ticksToFrames60(stage.ticksWave, physicsTps);
        log::info(
            "[{}] F6 Cycle -> {}: {} Ticks All | {} Ticks Wave ({:.1f}f @ 60fps, wave {:.1f}f)",
            modTag(),
            StageConfig::presetName(stage.index),
            stage.ticksAll,
            stage.ticksWave,
            frames60,
            waveFrames
        );
    }
    else {
        log::info(
            "[{}] F6 Cycle -> {}: {} Ticks All | {} Ticks Wave ({:.1f}f @ 60fps)",
            modTag(),
            StageConfig::presetName(stage.index),
            stage.ticksAll,
            stage.ticksWave,
            frames60
        );
    }
}

void AssistLog::printSavedPress(
    int delayedTicks,
    int frame0,
    int delayedFrame,
    int limit,
    double physicsTps
) {
    if (!StageConfig::debugLogging()) return;
    auto const frames60 = ticksToFrames60(delayedTicks, physicsTps);
    log::info(
        "[{}] *** SAVED +{} TICKS ({:.2f} frames @ 60fps) PRESS! (Frame0: {} -> Delayed: {}) [Limit: {}] ****",
        modTag(),
        delayedTicks,
        frames60,
        frame0,
        delayedFrame,
        limit
    );
}

