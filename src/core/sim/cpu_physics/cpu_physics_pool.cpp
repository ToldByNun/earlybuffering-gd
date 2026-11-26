#include "cpu_physics_pool.hpp"
#include "cpu_physics_gd_sim.hpp"
#include "cpu_physics_workers.hpp"
#include <algorithm>
#include <atomic>
#include <fmt/format.h>
#include <thread>
#include <vector>

unsigned EbCpuPhysicsPool::workerCount() {
    return EbCpuPhysicsWorkers::workerCount();
}

EbCpuPhysicsPool::ScreenBatch EbCpuPhysicsPool::screenDelays(ScreenRequest const& request) {
    ScreenBatch batch;
    if (!request.layer || !request.real || !request.world || !request.world->valid ||
        !request.world->seed.valid || request.limit < 0 || request.quantumTicks <= 0)
        return batch;

    if (!EbCpuPhysicsWorkers::ensure(request.layer))
        return batch;
    // Mode / collide-rect sync on main thread before workers touch ghosts.
    EbCpuPhysicsWorkers::seedFrom(request.layer, request.real);

    auto const& ghosts = EbCpuPhysicsWorkers::ghosts();
    PlayerObject* const templateGhost = EbCpuPhysicsWorkers::templateGhost();
    if (ghosts.empty() || !templateGhost)
        return batch;

    int const count = request.limit + 1;
    batch.byDelay.assign(static_cast<size_t>(count), EbCpuPhysicsRunResult{});
    batch.ok = true;

    auto runOne = [&](PlayerObject* ghost, int delay) {
        if (!ghost || delay < 0 || delay >= count)
            return;
        EbCpuPhysicsGdSim::Options opt;
        opt.down = request.down;
        opt.delay = delay;
        opt.maxTicks = std::max(request.quantumTicks, delay + 1);
        opt.deltaTime = request.deltaTime;
        opt.firstDt = request.firstDt;
        opt.releaseAfter = request.releaseAfter;
        batch.byDelay[static_cast<size_t>(delay)] =
            EbCpuPhysicsGdSim::run(ghost, templateGhost, *request.world, opt);
    };

    unsigned const workers = static_cast<unsigned>(ghosts.size());
    batch.workers = static_cast<int>(workers);
    if (workers <= 1u || count <= 2) {
        for (int delay = 0; delay < count; ++delay)
            runOne(ghosts[0], delay);
        batch.usedParallel = false;
        return batch;
    }

    batch.usedParallel = true;
    std::atomic<int> next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (unsigned index = 0; index < workers; ++index) {
        PlayerObject* ghost = ghosts[index];
        threads.emplace_back([&, ghost]() {
            for (;;) {
                int const delay = next.fetch_add(1, std::memory_order_relaxed);
                if (delay >= count)
                    break;
                runOne(ghost, delay);
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    return batch;
}

bool EbCpuPhysicsPool::selfCheck(std::string& report) {
    report.clear();
    std::string nested;
    if (!EbCpuPhysicsWorkers::selfCheck(nested)) {
        report = nested;
        return false;
    }
    if (!EbCpuPhysicsGdSim::selfCheck(nested)) {
        report = nested;
        return false;
    }
    if (workerCount() < 1u) {
        report = "CpuPhysicsPool selfCheck fails: workers";
        return false;
    }
    report = "CpuPhysicsPool selfCheck OK";
    return true;
}
