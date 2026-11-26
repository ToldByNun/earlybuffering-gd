#include "cpu_physics_adaptive.hpp"
#include "../foresight/travel_foresight.hpp"
#include "cpu_physics_pool.hpp"
#include "cpu_physics_workers.hpp"
#include "cpu_physics_world.hpp"
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

bool EbCpuPhysicsAdaptive::tryScreen(PlayLayer* layer, PlayerObject* real, bool down, int limit,
                                     float deltaTime, float firstDt, int quantumTicks,
                                     int releaseAfter, int baseAbsTick,
                                     std::vector<EbAdaptiveCandidateSlot>& slots,
                                     ScreenOutcome& outcome) {
    outcome = {};
    if (!layer || !real || limit < 0 || quantumTicks <= 0)
        return false;
    if (!EbCpuPhysicsWorkers::ensure(layer))
        return false;

    auto const t0 = std::chrono::steady_clock::now();
    float const ahead = EbTravelForesight::aheadX(real, limit, deltaTime, 8);

    EbCpuPhysicsWorldCapture::Options captureOpts;
    captureOpts.aheadX = ahead;
    captureOpts.behindX = EbSimulationWindow::kMinimumReach;
    captureOpts.horizonTicks = std::max(quantumTicks, limit + quantumTicks);
    captureOpts.baseAbsTick = std::max(0, baseAbsTick);
    captureOpts.simDt = deltaTime;

    auto world = EbCpuPhysicsWorldCapture::capture(layer, real, captureOpts);
    if (!world.valid)
        return false;

    EbCpuPhysicsPool::ScreenRequest req;
    req.layer = layer;
    req.real = real;
    req.world = &world;
    req.down = down;
    req.limit = limit;
    req.quantumTicks = quantumTicks;
    req.deltaTime = deltaTime;
    req.firstDt = firstDt;
    req.releaseAfter = releaseAfter;

    auto batch = EbCpuPhysicsPool::screenDelays(req);
    outcome.us = static_cast<int>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count());
    if (!batch.ok || static_cast<int>(batch.byDelay.size()) != limit + 1)
        return false;

    slots.assign(static_cast<size_t>(limit) + 1, EbAdaptiveCandidateSlot{});
    int survivors = 0;
    int screened = 0;
    for (int delay = 0; delay <= limit; ++delay) {
        auto const& raw = batch.byDelay[static_cast<size_t>(delay)];
        auto& slot = slots[static_cast<size_t>(delay)];
        slot.bindDelay(delay);

        EbShadowSimulator::RunResult mapped;
        mapped.died = raw.died;
        mapped.unknown = raw.unknown;
        mapped.unknownTrunc = raw.unknown;
        mapped.jumped = raw.jumped;
        mapped.activatedRing = raw.activatedBoost;
        mapped.ticksRan = raw.ticksRan;
        mapped.deathTick = raw.deathTick;
        mapped.jumpTick = raw.jumpTick;
        mapped.endX = raw.endX;
        mapped.deathX = raw.deathX;
        mapped.diedBeforeAction = raw.diedBeforeAction;
        mapped.ticksBeforeNeutral = raw.ticksRan;

        slot.applyQuantumResult(mapped, -1, raw.ticksRan, -1, delay == 0 || raw.ticksRan > delay,
                                false, down, false);
        slot.screened = true;
        ++screened;
        if (slot.autonomousAlive)
            ++survivors;
    }

    outcome.used = true;
    outcome.parallel = batch.usedParallel;
    outcome.workers = batch.workers;
    outcome.screened = screened;
    outcome.survivors = survivors;

    if (survivors <= 0) {
        outcome.fellBack = true;
        return false;
    }
    return true;
}

bool EbCpuPhysicsAdaptive::selfCheck(std::string& report) {
    report.clear();
    std::string nested;
    if (!EbCpuPhysicsWorldCapture::selfCheck(nested)) {
        report = nested;
        return false;
    }
    if (!EbCpuPhysicsPool::selfCheck(nested)) {
        report = nested;
        return false;
    }
    report = "CpuPhysicsAdaptive selfCheck OK";
    return true;
}
