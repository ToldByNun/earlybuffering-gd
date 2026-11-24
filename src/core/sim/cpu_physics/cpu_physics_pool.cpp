#include "cpu_physics_pool.hpp"
#include "cpu_physics_sim.hpp"
#include <algorithm>
#include <atomic>
#include <fmt/format.h>
#include <thread>
#include <vector>

unsigned EbCpuPhysicsPool::workerCount() {
    unsigned const hw = std::thread::hardware_concurrency();
    if (hw <= 1u)
        return 1u;
    // Leave one core for the game/render thread; cap so Adaptive screens stay snappy.
    return std::clamp(hw - 1u, 2u, 8u);
}

EbCpuPhysicsPool::ScreenBatch EbCpuPhysicsPool::screenDelays(ScreenRequest const& request) {
    ScreenBatch batch;
    if (!request.world || !request.world->valid || request.limit < 0 || request.quantumTicks <= 0)
        return batch;

    int const count = request.limit + 1;
    batch.byDelay.assign(static_cast<size_t>(count), EbCpuPhysicsRunResult{});
    batch.ok = true;

    auto runOne = [&](int delay) {
        if (delay < 0 || delay >= count)
            return;
        EbCpuPhysicsSim::Options opt;
        opt.down = request.down;
        opt.delay = delay;
        opt.maxTicks = std::max(request.quantumTicks, delay + 1);
        opt.deltaTime = request.deltaTime;
        opt.firstDt = request.firstDt;
        opt.releaseAfter = request.releaseAfter;
        batch.byDelay[static_cast<size_t>(delay)] = EbCpuPhysicsSim::run(*request.world, opt);
    };

    unsigned const workers = workerCount();
    batch.workers = static_cast<int>(workers);
    if (workers <= 1u || count <= 2) {
        for (int delay = 0; delay < count; ++delay)
            runOne(delay);
        batch.usedParallel = false;
        return batch;
    }

    batch.usedParallel = true;
    std::atomic<int> next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (unsigned index = 0; index < workers; ++index) {
        threads.emplace_back([&]() {
            for (;;) {
                int const delay = next.fetch_add(1, std::memory_order_relaxed);
                if (delay >= count)
                    break;
                runOne(delay);
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    return batch;
}

bool EbCpuPhysicsPool::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* label) {
        if (ok)
            return;
        ++fails;
        if (!report.empty())
            report += ", ";
        report += label;
    };

    expect(workerCount() >= 1u, "workers-ge1");

    EbCpuPhysicsWorld world;
    world.valid = true;
    world.player.x = 0.f;
    world.player.y = 50.f;
    world.player.w = 30.f;
    world.player.h = 30.f;
    world.player.grounded = true;
    world.floorY = 0.f;
    world.ceilY = 400.f;

    ScreenRequest req;
    req.world = &world;
    req.down = true;
    req.limit = 3;
    req.quantumTicks = 12;
    auto batch = screenDelays(req);
    expect(batch.ok, "batch-ok");
    expect(static_cast<int>(batch.byDelay.size()) == 4, "batch-size");
    expect(batch.byDelay[0].ticksRan > 0, "d0-ran");

    if (fails == 0) {
        report = "CpuPhysicsPool selfCheck OK";
        return true;
    }
    report = fmt::format("CpuPhysicsPool selfCheck {} fails: {}", fails, report);
    return false;
}
